#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orbit {

class QrScanner {
public:
    static constexpr int kPreviewWidth = 224;
    static constexpr int kPreviewHeight = 168;

    QrScanner();
    ~QrScanner();

    QrScanner(const QrScanner &) = delete;
    QrScanner &operator=(const QrScanner &) = delete;

    bool start();
    void stop();
    bool running() const;
    std::string status() const;
    bool consume_preview(std::vector<std::uint16_t> &pixels);
    bool consume_code(std::string &text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} 
