#include "orbit/qr_scanner.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#if !defined(ORBIT_DESKTOP_SIMULATOR)
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/orientation.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>
#include <linux/dma-buf.h>
#include <linux/videodev2.h>
#include <map>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#endif

namespace orbit {

struct QrScanner::Impl {
    mutable std::mutex mutex;
    std::thread worker;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> active{false};
    std::string status_text{"IDLE"};
    std::string decoded_text;
    std::vector<std::uint16_t> preview;
    bool preview_ready = false;
    bool code_ready = false;

    void set_status(std::string value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status_text = std::move(value);
    }

#if !defined(ORBIT_DESKTOP_SIMULATOR)
    struct Buffer {
        void *data = nullptr;
        std::size_t size = 0;
    };

    struct LibcameraCapture {
        struct MappedPlane {
            void *mapping = nullptr;
            std::size_t mapping_size = 0;
            std::size_t data_offset = 0;
            std::size_t data_size = 0;
            int fd = -1;
        };

        std::unique_ptr<libcamera::CameraManager> manager;
        std::shared_ptr<libcamera::Camera> camera;
        std::unique_ptr<libcamera::CameraConfiguration> config;
        std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
        libcamera::Stream *stream = nullptr;
        std::vector<std::unique_ptr<libcamera::Request>> requests;
        std::map<const libcamera::FrameBuffer *, MappedPlane> mappings;
        std::mutex frame_mutex;
        std::vector<std::uint8_t> pending_gray;
        int width = 0;
        int height = 0;
        int stride = 0;
        std::atomic<bool> streaming{false};
        bool manager_started = false;
        bool camera_acquired = false;
        bool frame_ready = false;

        static void sync_buffer(int fd, std::uint64_t flags)
        {
            if (fd < 0) return;
            dma_buf_sync sync{flags};
            (void)::ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
        }

        bool map_buffer(const libcamera::FrameBuffer *buffer)
        {
            const auto planes = buffer->planes();
            if (planes.empty()) return false;
            
            const auto &plane = planes.front();
            const long page_size_value = ::sysconf(_SC_PAGE_SIZE);
            const std::size_t page_size = page_size_value > 0
                                              ? static_cast<std::size_t>(page_size_value)
                                              : 4096U;
            const std::size_t map_offset = plane.offset & ~(page_size - 1U);
            const std::size_t data_offset = plane.offset - map_offset;
            const std::size_t map_size = data_offset + plane.length;
            void *mapping = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, plane.fd.get(), map_offset);
            if (mapping == MAP_FAILED) return false;
            mappings.emplace(buffer, MappedPlane{
                mapping, map_size, data_offset,
                static_cast<std::size_t>(plane.length), plane.fd.get(),
            });
            return true;
        }

        void request_complete(libcamera::Request *request)
        {
            if (!request || request->status() == libcamera::Request::RequestCancelled ||
                !streaming) return;
            libcamera::FrameBuffer *buffer = request->findBuffer(stream);
            const auto mapped = mappings.find(buffer);
            if (buffer && mapped != mappings.end()) {
                std::size_t bytes_used = mapped->second.data_size;
                const auto metadata_planes = buffer->metadata().planes();
                if (!metadata_planes.empty() && metadata_planes.front().bytesused > 0)
                    bytes_used = std::min(bytes_used,
                        static_cast<std::size_t>(metadata_planes.front().bytesused));
                const auto *source = static_cast<const std::uint8_t *>(
                    mapped->second.mapping) + mapped->second.data_offset;
                const std::size_t required = static_cast<std::size_t>(stride) * height;
                if (source && bytes_used >= required) {
                    sync_buffer(mapped->second.fd,
                                DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
                    std::vector<std::uint8_t> gray(
                        static_cast<std::size_t>(width) * height);
                    for (int y = 0; y < height; ++y) {
                        const std::uint8_t *row = source +
                            static_cast<std::size_t>(y) * stride;
                        for (int x = 0; x < width; ++x) {
                            const std::uint16_t pixel = static_cast<std::uint16_t>(
                                row[x * 2] | (row[x * 2 + 1] << 8U));
                            const unsigned red = ((pixel >> 11U) & 0x1FU) * 255U / 31U;
                            const unsigned green = ((pixel >> 5U) & 0x3FU) * 255U / 63U;
                            const unsigned blue = (pixel & 0x1FU) * 255U / 31U;
                            gray[static_cast<std::size_t>(y) * width + x] =
                                static_cast<std::uint8_t>((red * 77U + green * 150U +
                                                           blue * 29U) >> 8U);
                        }
                    }
                    sync_buffer(mapped->second.fd,
                                DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
                    std::lock_guard<std::mutex> lock(frame_mutex);
                    pending_gray = std::move(gray);
                    frame_ready = true;
                }
            }
            request->reuse(libcamera::Request::ReuseBuffers);
            if (camera && streaming) camera->queueRequest(request);
        }

        bool open(std::string &error)
        {
            manager = std::make_unique<libcamera::CameraManager>();
            if (manager->start()) {
                error = "CSI MANAGER FAILED";
                return false;
            }
            manager_started = true;
            for (const auto &candidate : manager->cameras()) {
                if (!camera) camera = candidate;
                std::string identity = candidate->id();
                std::transform(identity.begin(), identity.end(), identity.begin(),
                               [](unsigned char value) {
                                   return static_cast<char>(std::tolower(value));
                               });
                if (identity.find("imx219") != std::string::npos) {
                    camera = candidate;
                    break;
                }
            }
            if (!camera) {
                error = "CSI CAMERA NOT FOUND";
                return false;
            }
            if (camera->acquire()) {
                error = "CSI CAMERA BUSY";
                return false;
            }
            camera_acquired = true;
            config = camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
            if (!config || config->empty()) {
                error = "CSI CONFIG FAILED";
                return false;
            }
            libcamera::StreamConfiguration &stream_config = config->at(0);
            config->orientation = libcamera::Orientation::Rotate180;
            stream_config.size.width = 320;
            stream_config.size.height = 240;
            stream_config.pixelFormat = libcamera::formats::RGB565;
            stream_config.bufferCount = 4;
            if (config->validate() == libcamera::CameraConfiguration::Invalid ||
                camera->configure(config.get())) {
                error = "CSI CONFIG FAILED";
                return false;
            }
            const libcamera::StreamConfiguration &active_config = config->at(0);
            if (active_config.pixelFormat != libcamera::formats::RGB565) {
                error = "CSI FORMAT FAILED";
                return false;
            }
            stream = active_config.stream();
            width = static_cast<int>(active_config.size.width);
            height = static_cast<int>(active_config.size.height);
            stride = static_cast<int>(active_config.stride);
            if (!stream || width <= 0 || height <= 0 || stride < width * 2) {
                error = "CSI FORMAT FAILED";
                return false;
            }
            allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
            if (allocator->allocate(stream) < 0 || allocator->buffers(stream).empty()) {
                error = "CSI BUFFER FAILED";
                return false;
            }
            for (const auto &buffer : allocator->buffers(stream)) {
                if (!map_buffer(buffer.get())) continue;
                std::unique_ptr<libcamera::Request> request = camera->createRequest();
                if (!request || request->addBuffer(stream, buffer.get()) < 0) continue;
                requests.push_back(std::move(request));
            }
            if (requests.empty()) {
                error = "CSI BUFFER FAILED";
                return false;
            }
            camera->requestCompleted.connect(this, &LibcameraCapture::request_complete);
            if (camera->start()) {
                camera->requestCompleted.disconnect(this);
                error = "CSI START FAILED";
                return false;
            }
            streaming = true;
            for (const auto &request : requests) camera->queueRequest(request.get());
            return true;
        }

        bool consume(std::vector<std::uint8_t> &gray, int &frame_width,
                     int &frame_height)
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (!frame_ready) return false;
            gray = std::move(pending_gray);
            frame_width = width;
            frame_height = height;
            frame_ready = false;
            return true;
        }

        void close()
        {
            const bool was_streaming = streaming.exchange(false);
            if (camera) camera->requestCompleted.disconnect(this);
            if (camera && was_streaming) camera->stop();
            requests.clear();
            for (auto &item : mappings) {
                if (item.second.mapping && item.second.mapping != MAP_FAILED)
                    ::munmap(item.second.mapping, item.second.mapping_size);
            }
            mappings.clear();
            allocator.reset();
            stream = nullptr;
            config.reset();
            if (camera && camera_acquired) {
                camera->release();
            }
            camera_acquired = false;
            camera.reset();
            if (manager && manager_started) {
                manager->stop();
            }
            manager_started = false;
            manager.reset();
        }

        ~LibcameraCapture() { close(); }
    };

    struct ZbarApi {
        using Scanner = void;
        using Image = void;
        using Symbol = void;
        using Cleanup = void (*)(Image *);

        void *library = nullptr;
        Scanner *(*scanner_create)() = nullptr;
        void (*scanner_destroy)(Scanner *) = nullptr;
        int (*scanner_set_config)(Scanner *, int, int, int) = nullptr;
        Image *(*image_create)() = nullptr;
        void (*image_destroy)(Image *) = nullptr;
        void (*image_set_format)(Image *, unsigned long) = nullptr;
        void (*image_set_size)(Image *, unsigned, unsigned) = nullptr;
        void (*image_set_data)(Image *, const void *, unsigned long, Cleanup) = nullptr;
        int (*scan_image)(Scanner *, Image *) = nullptr;
        const Symbol *(*first_symbol)(const Image *) = nullptr;
        const Symbol *(*next_symbol)(const Symbol *) = nullptr;
        int (*symbol_type)(const Symbol *) = nullptr;
        const char *(*symbol_data)(const Symbol *) = nullptr;
        unsigned (*symbol_data_length)(const Symbol *) = nullptr;

        template <typename T>
        bool load_symbol(T &output, const char *name)
        {
            output = reinterpret_cast<T>(dlsym(library, name));
            return output != nullptr;
        }

        bool open()
        {
            library = dlopen("libzbar.so.0", RTLD_NOW | RTLD_LOCAL);
            if (!library) return false;
            const bool loaded =
                load_symbol(scanner_create, "zbar_image_scanner_create") &&
                load_symbol(scanner_destroy, "zbar_image_scanner_destroy") &&
                load_symbol(scanner_set_config, "zbar_image_scanner_set_config") &&
                load_symbol(image_create, "zbar_image_create") &&
                load_symbol(image_destroy, "zbar_image_destroy") &&
                load_symbol(image_set_format, "zbar_image_set_format") &&
                load_symbol(image_set_size, "zbar_image_set_size") &&
                load_symbol(image_set_data, "zbar_image_set_data") &&
                load_symbol(scan_image, "zbar_scan_image") &&
                load_symbol(first_symbol, "zbar_image_first_symbol") &&
                load_symbol(next_symbol, "zbar_symbol_next") &&
                load_symbol(symbol_type, "zbar_symbol_get_type") &&
                load_symbol(symbol_data, "zbar_symbol_get_data") &&
                load_symbol(symbol_data_length, "zbar_symbol_get_data_length");
            if (!loaded) close();
            return loaded;
        }

        void close()
        {
            if (library) dlclose(library);
            *this = {};
        }
    };

    static bool ioctl_retry(int fd, unsigned long request, void *argument)
    {
        int result = 0;
        do {
            result = ::ioctl(fd, request, argument);
        } while (result < 0 && errno == EINTR);
        return result == 0;
    }

    static std::vector<std::string> camera_candidates()
    {
        std::vector<std::string> result;
        const auto add = [&result](const char *path) {
            if (!path || !*path) return;
            if (std::find(result.begin(), result.end(), path) == result.end())
                result.emplace_back(path);
        };
        add(std::getenv("ORBIT_CAMERA_DEVICE"));
        add(std::getenv("CAMERA_APP_V4L2_DEVICE"));
        add(std::getenv("V4L2_CAMERA_DEVICE"));
        for (int index = 0; index < 16; ++index)
            add(("/dev/video" + std::to_string(index)).c_str());
        return result;
    }

    static bool configure_camera(int fd, int &width, int &height,
                                 int &stride, std::uint32_t &pixel_format)
    {
        struct Mode {
            std::uint32_t format;
            int width;
            int height;
        };
        static constexpr Mode modes[] = {
            {V4L2_PIX_FMT_GREY, 320, 240},
            {V4L2_PIX_FMT_YUYV, 320, 240},
            {V4L2_PIX_FMT_UYVY, 320, 240},
            {V4L2_PIX_FMT_GREY, 640, 480},
            {V4L2_PIX_FMT_YUYV, 640, 480},
            {V4L2_PIX_FMT_UYVY, 640, 480},
        };
        for (const Mode &mode : modes) {
            v4l2_format format{};
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            format.fmt.pix.width = static_cast<std::uint32_t>(mode.width);
            format.fmt.pix.height = static_cast<std::uint32_t>(mode.height);
            format.fmt.pix.pixelformat = mode.format;
            format.fmt.pix.field = V4L2_FIELD_ANY;
            if (!ioctl_retry(fd, VIDIOC_S_FMT, &format)) continue;
            if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_GREY &&
                format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV &&
                format.fmt.pix.pixelformat != V4L2_PIX_FMT_UYVY) continue;
            width = static_cast<int>(format.fmt.pix.width);
            height = static_cast<int>(format.fmt.pix.height);
            pixel_format = format.fmt.pix.pixelformat;
            stride = static_cast<int>(format.fmt.pix.bytesperline);
            if (stride <= 0)
                stride = width * (pixel_format == V4L2_PIX_FMT_GREY ? 1 : 2);
            return width > 0 && height > 0;
        }
        return false;
    }

    static bool make_grayscale(const std::uint8_t *source, std::size_t bytes_used,
                               int width, int height, int stride,
                               std::uint32_t pixel_format,
                               std::vector<std::uint8_t> &gray)
    {
        const int pixel_stride = pixel_format == V4L2_PIX_FMT_GREY ? 1 : 2;
        const int luma_offset = pixel_format == V4L2_PIX_FMT_UYVY ? 1 : 0;
        if (!source || width <= 0 || height <= 0 || stride <= 0 ||
            bytes_used < static_cast<std::size_t>(stride) * height) return false;
        gray.resize(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            const std::uint8_t *row = source + static_cast<std::size_t>(y) * stride;
            std::uint8_t *output = gray.data() + static_cast<std::size_t>(y) * width;
            for (int x = 0; x < width; ++x)
                output[x] = row[x * pixel_stride + luma_offset];
        }
        return true;
    }

    void publish_preview(const std::vector<std::uint8_t> &gray, int width, int height)
    {
        std::vector<std::uint16_t> frame(
            static_cast<std::size_t>(QrScanner::kPreviewWidth) * QrScanner::kPreviewHeight);
        for (int y = 0; y < QrScanner::kPreviewHeight; ++y) {
            const int source_y = y * height / QrScanner::kPreviewHeight;
            for (int x = 0; x < QrScanner::kPreviewWidth; ++x) {
                const int source_x = x * width / QrScanner::kPreviewWidth;
                const std::uint8_t value =
                    gray[static_cast<std::size_t>(source_y) * width + source_x];
                frame[static_cast<std::size_t>(y) * QrScanner::kPreviewWidth + x] =
                    static_cast<std::uint16_t>(((value >> 3U) << 11U) |
                                               ((value >> 2U) << 5U) |
                                               (value >> 3U));
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        preview = std::move(frame);
        preview_ready = true;
    }

    void scan_frame(ZbarApi &zbar, ZbarApi::Scanner *scanner,
                    const std::vector<std::uint8_t> &gray, int width, int height)
    {
        constexpr unsigned long kY800 =
            static_cast<unsigned long>('Y') |
            (static_cast<unsigned long>('8') << 8U) |
            (static_cast<unsigned long>('0') << 16U) |
            (static_cast<unsigned long>('0') << 24U);
        constexpr int kQrCode = 64;
        ZbarApi::Image *image = zbar.image_create();
        if (!image) return;
        zbar.image_set_format(image, kY800);
        zbar.image_set_size(image, static_cast<unsigned>(width),
                            static_cast<unsigned>(height));
        zbar.image_set_data(image, gray.data(), gray.size(), nullptr);
        if (zbar.scan_image(scanner, image) > 0) {
            for (const ZbarApi::Symbol *symbol = zbar.first_symbol(image);
                 symbol; symbol = zbar.next_symbol(symbol)) {
                if (zbar.symbol_type(symbol) != kQrCode) continue;
                const char *data = zbar.symbol_data(symbol);
                const unsigned length = zbar.symbol_data_length(symbol);
                if (!data || length == 0) continue;
                std::lock_guard<std::mutex> lock(mutex);
                decoded_text.assign(data, data + length);
                code_ready = true;
                break;
            }
        }
        zbar.image_destroy(image);
    }

    void run()
    {
        active = true;
        set_status("OPENING CAMERA...");
        ZbarApi zbar;
        if (!zbar.open()) {
            set_status("QR DECODER NOT FOUND");
            active = false;
            return;
        }
        ZbarApi::Scanner *scanner = zbar.scanner_create();
        if (!scanner) {
            zbar.close();
            set_status("QR DECODER FAILED");
            active = false;
            return;
        }
        constexpr int kAllSymbols = 0;
        constexpr int kQrCode = 64;
        constexpr int kEnable = 0;
        zbar.scanner_set_config(scanner, kAllSymbols, kEnable, 0);
        zbar.scanner_set_config(scanner, kQrCode, kEnable, 1);

        std::string csi_error;
        LibcameraCapture csi;
        if (csi.open(csi_error)) {
            set_status("SCANNING CSI...");
            std::vector<std::uint8_t> grayscale;
            int width = 0;
            int height = 0;
            std::uint32_t frame_counter = 0;
            while (!stop_requested) {
                if (!csi.consume(grayscale, width, height)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if ((frame_counter++ & 1U) == 0U)
                    publish_preview(grayscale, width, height);
                scan_frame(zbar, scanner, grayscale, width, height);
            }
            csi.close();
            zbar.scanner_destroy(scanner);
            zbar.close();
            active = false;
            return;
        }
        csi.close();
        set_status(csi_error + "\nTRYING USB...");

        int fd = -1;
        int width = 0;
        int height = 0;
        int stride = 0;
        std::uint32_t pixel_format = 0;
        for (const std::string &candidate : camera_candidates()) {
            const int candidate_fd = ::open(candidate.c_str(), O_RDWR | O_NONBLOCK);
            if (candidate_fd < 0) continue;
            v4l2_capability capability{};
            if (ioctl_retry(candidate_fd, VIDIOC_QUERYCAP, &capability)) {
                const std::uint32_t caps =
                    (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
                        ? capability.device_caps : capability.capabilities;
                if ((caps & V4L2_CAP_VIDEO_CAPTURE) &&
                    (caps & V4L2_CAP_STREAMING) &&
                    configure_camera(candidate_fd, width, height, stride,
                                     pixel_format)) {
                    fd = candidate_fd;
                    break;
                }
            }
            ::close(candidate_fd);
        }

        std::vector<Buffer> buffers;
        bool streaming = false;
        if (fd < 0) {
            set_status(csi_error + "\nUSB NOT FOUND");
        }
        else {
            v4l2_requestbuffers request{};
            request.count = 4;
            request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            request.memory = V4L2_MEMORY_MMAP;
            if (ioctl_retry(fd, VIDIOC_REQBUFS, &request) && request.count >= 2) {
                buffers.resize(request.count);
                bool mapped = true;
                for (std::uint32_t index = 0; index < request.count; ++index) {
                    v4l2_buffer buffer{};
                    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buffer.memory = V4L2_MEMORY_MMAP;
                    buffer.index = index;
                    if (!ioctl_retry(fd, VIDIOC_QUERYBUF, &buffer)) {
                        mapped = false;
                        break;
                    }
                    buffers[index].size = buffer.length;
                    buffers[index].data = ::mmap(nullptr, buffer.length,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, fd, buffer.m.offset);
                    if (buffers[index].data == MAP_FAILED) {
                        buffers[index].data = nullptr;
                        mapped = false;
                        break;
                    }
                }
                if (mapped) {
                    for (std::uint32_t index = 0; index < buffers.size(); ++index) {
                        v4l2_buffer buffer{};
                        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buffer.memory = V4L2_MEMORY_MMAP;
                        buffer.index = index;
                        if (!ioctl_retry(fd, VIDIOC_QBUF, &buffer)) {
                            mapped = false;
                            break;
                        }
                    }
                }
                if (mapped) {
                    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    streaming = ioctl_retry(fd, VIDIOC_STREAMON, &type);
                }
            }
            if (!streaming) set_status(csi_error + "\nUSB START FAILED");
        }

        if (streaming) {
            set_status("SCANNING...");
            std::vector<std::uint8_t> grayscale;
            std::uint32_t frame_counter = 0;
            while (!stop_requested) {
                pollfd descriptor{fd, POLLIN, 0};
                const int poll_result = ::poll(&descriptor, 1, 100);
                if (poll_result < 0 && errno != EINTR) {
                    set_status("CAMERA READ FAILED");
                    break;
                }
                if (poll_result <= 0) continue;
                v4l2_buffer buffer{};
                buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buffer.memory = V4L2_MEMORY_MMAP;
                if (!ioctl_retry(fd, VIDIOC_DQBUF, &buffer)) {
                    if (errno != EAGAIN) set_status("CAMERA READ FAILED");
                    continue;
                }
                if (buffer.index < buffers.size() &&
                    make_grayscale(static_cast<const std::uint8_t *>(buffers[buffer.index].data),
                                   buffer.bytesused, width, height, stride,
                                   pixel_format, grayscale)) {
                    if ((frame_counter++ & 1U) == 0U)
                        publish_preview(grayscale, width, height);
                    scan_frame(zbar, scanner, grayscale, width, height);
                }
                ioctl_retry(fd, VIDIOC_QBUF, &buffer);
            }
        }

        if (streaming) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl_retry(fd, VIDIOC_STREAMOFF, &type);
        }
        for (Buffer &buffer : buffers) {
            if (buffer.data) ::munmap(buffer.data, buffer.size);
        }
        if (fd >= 0) ::close(fd);
        zbar.scanner_destroy(scanner);
        zbar.close();
        active = false;
    }
#endif
};

QrScanner::QrScanner() : impl_(std::make_unique<Impl>()) {}

QrScanner::~QrScanner() { stop(); }

bool QrScanner::start()
{
    stop();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->decoded_text.clear();
        impl_->preview.clear();
        impl_->preview_ready = false;
        impl_->code_ready = false;
    }
#if defined(ORBIT_DESKTOP_SIMULATOR)
    impl_->set_status("CAMERA SCANNING\nIS AVAILABLE ON DEVICE");
    return false;
#else
    impl_->stop_requested = false;
    impl_->worker = std::thread([this] { impl_->run(); });
    return true;
#endif
}

void QrScanner::stop()
{
    impl_->stop_requested = true;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->active = false;
}

bool QrScanner::running() const { return impl_->active; }

std::string QrScanner::status() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status_text;
}

bool QrScanner::consume_preview(std::vector<std::uint16_t> &pixels)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->preview_ready) return false;
    pixels = impl_->preview;
    impl_->preview_ready = false;
    return true;
}

bool QrScanner::consume_code(std::string &text)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->code_ready) return false;
    text = impl_->decoded_text;
    impl_->code_ready = false;
    return true;
}

} 
