
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#if !LV_USE_SDL
#include <sys/timerfd.h>
#include <linux/input.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#else
#include "compat/input_keys.h"
#endif

                                                               
                
                                                                  
struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_HOME_KEY_FLAGE = 0;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD;
#if !LV_USE_SDL
                                                               
          
                                                                  
#define EVDEV_KEYCODE_OFFSET   8
#define REPEAT_DELAY_MS      500                              
#define REPEAT_RATE_MS        30                                 

                                                               
                              
                                                                  
static int open_restricted(const char *path, int flags, void *user_data) {
    int fd = open(path, flags);
    if (fd < 0)
        fprintf(stderr, "无法打开 %s: %s\n", path, strerror(errno));
    return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *user_data) { close(fd); }
static const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

                                                               
                                                               
                                                                  
struct tca8418_keymap_entry {
    uint32_t    keycode;
    const char *sym_name;
    const char *utf8;
};
static const struct tca8418_keymap_entry tca8418_keymap[] = {
    { 183, "exclam",       "!"  },

    { 184, "at",           "@"  },
    { 185, "numbersign",   "#"  },
    { 186, "dollar",       "$"  },
    { 187, "percent",      "%"  },
    { 188, "asciicircum",  "^"  },
    { 189, "ampersand",    "&"  },
    { 190, "asterisk",     "*"  },
    { 191, "parenleft",    "("  },
    { 192, "parenright",   ")"  },

    { 193, "asciitilde",   "~"  },
    { 194, "grave",        "`"  },
    { 195, "plus",         "+"  },
    { 196, "minus",        "-"  },
    { 197, "slash",        "/"  },
    { 198, "backslash",    "\\" },
    { 199, "braceleft",    "{"  },
    { 200, "braceright",   "}"  },
    { 201, "bracketleft",  "["  },
    { 202, "bracketright", "]"  },

    { 231, "comma",        ","  },
    { 232, "period",       "."  },
    { 233, "bar",          "|"  },
    { 209, "equal",        "="  },
    { 210, "colon",        ":"  },
    { 211, "semicolon",    ";"  },
    { 212, "underscore",   "_"  },
    { 213, "question",     "?"  },

    { 214, "less",         "<"  },
    { 215, "greater",      ">"  },
    { 216, "apostrophe",   "'"  },
    { 217, "quotedbl",     "\"" },
};









#define TCA8418_KEYMAP_SIZE (sizeof(tca8418_keymap)/sizeof(tca8418_keymap[0]))

static const struct tca8418_keymap_entry *
tca8418_keymap_lookup(uint32_t keycode) {
    for (size_t i = 0; i < TCA8418_KEYMAP_SIZE; i++)
        if (tca8418_keymap[i].keycode == keycode)
            return &tca8418_keymap[i];
    return NULL;
}


                                                               
                                             
                                                                                 
                                                                  
struct ctrl_key_utf8_entry {
    uint32_t    keycode;                                     
    const char *utf8;                                                  
};

static const struct ctrl_key_utf8_entry ctrl_key_utf8_map[] = {
                                         
    { KEY_ENTER,     "\r"      },                   
    { KEY_KPENTER,   "\r"      },                            
    { KEY_BACKSPACE, "\x7f"    },                   
    { KEY_TAB,       "\t"      },                   
    { KEY_ESC,       "\x1b"    },                   

                                  
    { KEY_UP,        "\033[A"  },              
    { KEY_DOWN,      "\033[B"  },              
    { KEY_RIGHT,     "\033[C"  },              
    { KEY_LEFT,      "\033[D"  },              

                                  
    { KEY_HOME,      "\033[H"  },                               
    { KEY_END,       "\033[F"  },                               
    { KEY_DELETE,    "\033[3~" },              
    { KEY_INSERT,    "\033[2~" },              
    { KEY_PAGEUP,    "\033[5~" },              
    { KEY_PAGEDOWN,  "\033[6~" },              

                          
    { KEY_F1,        "\033OP"  },
    { KEY_F2,        "\033OQ"  },
    { KEY_F3,        "\033OR"  },
    { KEY_F4,        "\033OS"  },
    { KEY_F5,        "\033[15~"},
    { KEY_F6,        "\033[17~"},
    { KEY_F7,        "\033[18~"},
    { KEY_F8,        "\033[19~"},
    { KEY_F9,        "\033[20~"},
    { KEY_F10,       "\033[21~"},
    { KEY_F11,       "\033[23~"},
    { KEY_F12,       "\033[24~"},
};

static const char *ctrl_key_utf8_lookup(uint32_t keycode) {
    for (size_t i = 0; i < sizeof(ctrl_key_utf8_map) / sizeof(ctrl_key_utf8_map[0]); i++)
        if (ctrl_key_utf8_map[i].keycode == keycode)
            return ctrl_key_utf8_map[i].utf8;
    return NULL;
}





                                                               
                   
                                                                  
struct kbd_ctx {
    struct libinput        *li;
    struct libinput_device *dev;

    struct xkb_context        *ctx;
    struct xkb_keymap         *keymap;
    struct xkb_state          *state;
    struct xkb_compose_table  *compose_table;
    struct xkb_compose_state  *compose_state;

                      
    int      repeat_fd;
    bool     repeating;
    struct key_item repeat_template;                                                         
};

                                                               
                          
                                                                  
static void uxkb_log(struct xkb_context *ctx, enum xkb_log_level level,
                     const char *fmt, va_list args)
{
    (void)ctx; (void)level;
    vfprintf(stderr, fmt, args);
}

                                                               
                   
                                                                  
static uint32_t get_mods(struct xkb_state *state) {
    uint32_t m = 0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_CTRL;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_ALT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_LOGO;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_CAPS;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_EFFECTIVE) > 0)
        m |= KBD_MOD_NUM;
    return m;
}

                                                               
                                                           
                                                                  
static void update_leds(struct kbd_ctx *kc) {
    enum libinput_led leds = 0;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_NUM) > 0)
        leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_CAPS) > 0)
        leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kc->state, XKB_LED_NAME_SCROLL) > 0)
        leds |= LIBINPUT_LED_SCROLL_LOCK;
    libinput_device_led_update(kc->dev, leds);
}

                                                               
          
                                                                  
static void enqueue_key(const struct key_item *src) {
    struct key_item *elm = malloc(sizeof(*elm));
    if (!elm) return;
    *elm = *src;
    elm->flage = 0;  


    if(elm->key_code == KEY_ESC) {
        LVGL_HOME_KEY_FLAGE = elm->key_state;
    }

    if(LVGL_RUN_FLAGE)
    {
        pthread_mutex_lock(&keyboard_mutex);
        STAILQ_INSERT_TAIL(&keyboard_queue, elm, entries);
        pthread_mutex_unlock(&keyboard_mutex);
    }
    else
    {
        free(elm);
    }

}

                                                               
                      
                                                                  
static void repeat_start(struct kbd_ctx *kc) {
    struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = (long)REPEAT_RATE_MS  * 1000000L },
        .it_value    = { .tv_sec = 0, .tv_nsec = (long)REPEAT_DELAY_MS * 1000000L },
    };
    timerfd_settime(kc->repeat_fd, 0, &ts, NULL);
    kc->repeating = true;
}
static void repeat_stop(struct kbd_ctx *kc) {
    struct itimerspec ts = {0};
    timerfd_settime(kc->repeat_fd, 0, &ts, NULL);
    kc->repeating = false;
}

                                                        
static int utf32_to_utf8(uint32_t cp, char *out, size_t n) {
    if (n < 1) return 0;
    if (cp < 0x80) {
        if (n < 2) return 0;
        out[0] = (char)cp; out[1] = '\0'; return 1;
    } else if (cp < 0x800) {
        if (n < 3) return 0;
        out[0] = 0xC0 | (cp >> 6);
        out[1] = 0x80 | (cp & 0x3F);
        out[2] = '\0'; return 2;
    } else if (cp < 0x10000) {
        if (n < 4) return 0;
        out[0] = 0xE0 | (cp >> 12);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        out[3] = '\0'; return 3;
    } else if (cp < 0x110000) {
        if (n < 5) return 0;
        out[0] = 0xF0 | (cp >> 18);
        out[1] = 0x80 | ((cp >> 12) & 0x3F);
        out[2] = 0x80 | ((cp >> 6)  & 0x3F);
        out[3] = 0x80 | (cp & 0x3F);
        out[4] = '\0'; return 4;
    }
    out[0] = '\0'; return 0;
}

                                                               
                                     
                                                                  
static void process_key(struct kbd_ctx *kc, uint32_t code, int pressed)
{
    xkb_keycode_t keycode = code + EVDEV_KEYCODE_OFFSET;
    struct key_item item = {0};
    item.key_code  = code;
    item.key_state = pressed ? KBD_KEY_PRESSED : KBD_KEY_RELEASED;

                                                                
    const struct tca8418_keymap_entry *mapped = tca8418_keymap_lookup(code);
    if (mapped) {
        xkb_keysym_t sym = xkb_keysym_from_name(mapped->sym_name,
                                                XKB_KEYSYM_NO_FLAGS);
        snprintf(item.sym_name, sizeof(item.sym_name), "%s", mapped->sym_name);
        snprintf(item.utf8,     sizeof(item.utf8),     "%s", mapped->utf8);
        item.keysym    = sym;
        item.codepoint = (sym != XKB_KEY_NoSymbol) ? xkb_keysym_to_utf32(sym) : 0;
        item.mods      = get_mods(kc->state);

                          
        if (pressed) {
            kc->repeat_template = item;
            kc->repeat_template.key_state = KBD_KEY_REPEATED;
            repeat_start(kc);
        } else if (kc->repeating && kc->repeat_template.key_code == code) {
            repeat_stop(kc);
        }
        enqueue_key(&item);
        return;
    }

                                                          
    const xkb_keysym_t *syms;
    int num = xkb_state_key_get_syms(kc->state, keycode, &syms);
    xkb_keysym_t one_sym = XKB_KEY_NoSymbol;

    if (num == 1) {
                                                                                
        one_sym = xkb_state_key_get_one_sym(kc->state, keycode);
    } else if (num > 1) {
        one_sym = syms[0];
    }

                                                                
    enum xkb_compose_status cstatus = XKB_COMPOSE_NOTHING;
    bool compose_produced_utf8 = false;

    if (kc->compose_state && pressed) {
        xkb_compose_state_feed(kc->compose_state, one_sym);
        cstatus = xkb_compose_state_get_status(kc->compose_state);

        if (cstatus == XKB_COMPOSE_COMPOSED) {
            xkb_keysym_t csym = xkb_compose_state_get_one_sym(kc->compose_state);
            if (csym != XKB_KEY_NoSymbol) {
                one_sym = csym;
            }
                                              
            int n = xkb_compose_state_get_utf8(kc->compose_state,
                                               item.utf8, sizeof(item.utf8));
            if (n > 0) compose_produced_utf8 = true;

                                                                          
            if (csym == XKB_KEY_NoSymbol && !compose_produced_utf8)
                cstatus = XKB_COMPOSE_CANCELLED;
        }
        if (cstatus == XKB_COMPOSE_COMPOSED || cstatus == XKB_COMPOSE_CANCELLED)
            xkb_compose_state_reset(kc->compose_state);
    }

                                                                                  
    enum xkb_state_component changed = 0;
    if (pressed)
        changed = xkb_state_update_key(kc->state, keycode, XKB_KEY_DOWN);
    else
        changed = xkb_state_update_key(kc->state, keycode, XKB_KEY_UP);
    if (changed & XKB_STATE_LEDS)
        update_leds(kc);

                                                                                
    if (cstatus == XKB_COMPOSE_COMPOSING || cstatus == XKB_COMPOSE_CANCELLED)
        return;
    if (num <= 0 && !compose_produced_utf8)
        return;

                                              
    xkb_keysym_get_name(one_sym, item.sym_name, sizeof(item.sym_name));
    item.keysym    = one_sym;
    item.codepoint = (one_sym != XKB_KEY_NoSymbol)
                         ? xkb_keysym_to_utf32(one_sym) : 0;
    item.mods      = get_mods(kc->state);

                                                              
    if (item.utf8[0] == '\0') {
        xkb_state_key_get_utf8(kc->state, keycode,
                               item.utf8, sizeof(item.utf8));
        if (item.utf8[0] == '\0' && item.codepoint != 0) {
                                                                        
            utf32_to_utf8(item.codepoint, item.utf8, sizeof(item.utf8));
        }
        if (item.codepoint == 0)
            item.codepoint = xkb_state_key_get_utf32(kc->state, keycode);
    }
    
                                                         
                                                                               
                                                                              
    if (item.utf8[0] == '\0') {
        const char *ctrl = ctrl_key_utf8_lookup(code);
        if (ctrl) {
            snprintf(item.utf8, sizeof(item.utf8), "%s", ctrl);
        }
    }

                                               
    if (pressed && xkb_keymap_key_repeats(kc->keymap, keycode)) {
        kc->repeat_template = item;
        kc->repeat_template.key_state = KBD_KEY_REPEATED;
        repeat_start(kc);
    } else if (!pressed && kc->repeating &&
               kc->repeat_template.key_code == code) {
        repeat_stop(kc);
    }

                                         
    enqueue_key(&item);
}

                                                               
                                                 
                                                                  
static int init_xkb(struct kbd_ctx *kc,
                    const char *layout, const char *locale)
{
    kc->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!kc->ctx) { fprintf(stderr, "xkb_context_new 失败\n"); return -1; }
    xkb_context_set_log_fn(kc->ctx, uxkb_log);

    struct xkb_rule_names rmlvo = {
        .rules   = "evdev",
        .model   = NULL,
        .layout  = layout ? layout : "us",
        .variant = NULL,
        .options = NULL,
    };
    kc->keymap = xkb_keymap_new_from_names(kc->ctx, &rmlvo,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!kc->keymap) {
                              
        struct xkb_rule_names empty = {0};
        kc->keymap = xkb_keymap_new_from_names(kc->ctx, &empty,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!kc->keymap) { fprintf(stderr, "创建 keymap 失败\n"); return -1; }

    kc->state = xkb_state_new(kc->keymap);
    if (!kc->state) { fprintf(stderr, "创建 xkb_state 失败\n"); return -1; }

                     
    if (!locale || !*locale) {
        locale = getenv("LC_ALL");
        if (!locale || !*locale) locale = getenv("LC_CTYPE");
        if (!locale || !*locale) locale = getenv("LANG");
        if (!locale || !*locale) locale = "C";
    }
    kc->compose_table = xkb_compose_table_new_from_locale(
        kc->ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (kc->compose_table) {
        kc->compose_state = xkb_compose_state_new(kc->compose_table,
                                                  XKB_COMPOSE_STATE_NO_FLAGS);
        if (!kc->compose_state)
            fprintf(stderr, "警告：创建 compose_state 失败，禁用 compose\n");
    } else {
        fprintf(stderr, "警告：locale=%s 无 compose 表\n", locale);
    }
    return 0;
}

static void free_xkb(struct kbd_ctx *kc) {
    if (kc->compose_state) xkb_compose_state_unref(kc->compose_state);
    if (kc->compose_table) xkb_compose_table_unref(kc->compose_table);
    if (kc->state)  xkb_state_unref(kc->state);
    if (kc->keymap) xkb_keymap_unref(kc->keymap);
    if (kc->ctx)    xkb_context_unref(kc->ctx);
}

                                                                                                
static void kbd_wake_up(struct kbd_ctx *kc) {
    xkb_mod_mask_t locked_mods = xkb_state_serialize_mods(kc->state,
                                                          XKB_STATE_MODS_LOCKED);
    xkb_layout_index_t locked_layout = xkb_state_serialize_layout(
        kc->state, XKB_STATE_LAYOUT_LOCKED);
    xkb_state_unref(kc->state);
    kc->state = xkb_state_new(kc->keymap);
    if (!kc->state) return;
    xkb_state_update_mask(kc->state, 0, 0, locked_mods, 0, 0, locked_layout);
    update_leds(kc);
    if (kc->compose_state) xkb_compose_state_reset(kc->compose_state);
}

                                                               
                   
                                                                  
void *keyboard_read_thread(void *argv) {
    STAILQ_INIT(&keyboard_queue);

    const char *device_path = argv ? (const char *)argv
        : "/dev/input/by-path/platform-3f804000.i2c-event";

    struct kbd_ctx kc = {0};
    kc.repeat_fd = -1;

                                           
    kc.li = libinput_path_create_context(&interface, NULL);
    if (!kc.li) { fprintf(stderr, "创建 libinput 上下文失败\n"); goto out; }

    kc.dev = libinput_path_add_device(kc.li, device_path);
    if (!kc.dev) {
        fprintf(stderr, "添加设备 %s 失败（可能需要 root 权限）\n", device_path);
        goto out;
    }
    if (!libinput_device_has_capability(kc.dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        fprintf(stderr, "%s 不是键盘设备\n", device_path);
        goto out;
    }

                                            
    if (init_xkb(&kc, "us", NULL) < 0) goto out;

                                                       
    kc.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (kc.repeat_fd < 0) { perror("timerfd_create"); goto out; }

                                               
    int li_fd = libinput_get_fd(kc.li);
    struct pollfd pfds[2] = {
        { .fd = li_fd,        .events = POLLIN },
        { .fd = kc.repeat_fd, .events = POLLIN },
    };

    printf("开始监听键盘输入 (%s)\n", device_path);
    libinput_dispatch(kc.li);

    while (1) {
        int pr = poll(pfds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }

                          
        if (pfds[0].revents & POLLIN) {
            libinput_dispatch(kc.li);
            struct libinput_event *ev;
            while ((ev = libinput_get_event(kc.li)) != NULL) {
                if (libinput_event_get_type(ev) == LIBINPUT_EVENT_KEYBOARD_KEY) {
                    struct libinput_event_keyboard *kev =
                        libinput_event_get_keyboard_event(ev);
                    uint32_t code = libinput_event_keyboard_get_key(kev);
                    enum libinput_key_state ks =
                        libinput_event_keyboard_get_key_state(kev);
                    process_key(&kc, code,
                                ks == LIBINPUT_KEY_STATE_PRESSED ? 1 : 0);
                }
                libinput_event_destroy(ev);
            }
        }

                                   
        if (pfds[1].revents & POLLIN) {
            uint64_t exp;
            while (read(kc.repeat_fd, &exp, sizeof(exp)) == sizeof(exp)) {
                if (kc.repeating) {
                                                                                   
                    kc.repeat_template.mods = get_mods(kc.state);
                    enqueue_key(&kc.repeat_template);
                }
            }
        }
    }

out:
    if (kc.repeat_fd >= 0) close(kc.repeat_fd);
    free_xkb(&kc);
    if (kc.li) libinput_unref(kc.li);
    return NULL;
}
#else

#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/core/lv_group.h"
#include "lvgl/src/core/lv_obj.h"
#include "lvgl/src/core/lv_obj_event.h"
#include "lvgl/src/display/lv_display.h"
#include "lvgl/src/stdlib/lv_string.h"
#include "lvgl/src/misc/lv_text_private.h"
#include "lvgl/src/drivers/sdl/lv_sdl_private.h"

#include <stdlib.h>
#include <string.h>

                                                                         


                                                     
#ifndef KBD_MOD_SHIFT
#define KBD_MOD_SHIFT   (1u << 0)
#define KBD_MOD_CTRL    (1u << 1)
#define KBD_MOD_ALT     (1u << 2)
#define KBD_MOD_LOGO    (1u << 3)
#endif

                       
                
                        
typedef struct {
    char buf[KEYBOARD_BUFFER_SIZE];
    bool dummy_read;

    uint32_t cur_scancode;
    uint32_t cur_keysym;
    uint32_t cur_mods;
    int      cur_key_state;
    char     cur_sym_name[65];
    bool     cur_valid;

                                                                         
    uint32_t last_codepoint;
    char     last_utf8[8];
    size_t   last_utf8_len;
} lv_sdl_keyboard_t;

                       
                     
                        
static void sdl_keyboard_read(lv_indev_t * indev, lv_indev_data_t * data);
static uint32_t keycode_to_ctrl_key(SDL_Keycode sdl_key);
static uint32_t sdl_key_to_input_key(SDL_Keycode sdl_key, SDL_Scancode scancode);
static void release_indev_cb(lv_event_t * e);
static void send_key_item_event(lv_sdl_keyboard_t * dev,
                                uint32_t codepoint,
                                const char * utf8, size_t utf8_len,
                                int key_state);

                       
                     
                        
lv_indev_t * lv_sdl_keyboard_create(void)
{
    lv_sdl_keyboard_t * dsc = lv_malloc_zeroed(sizeof(lv_sdl_keyboard_t));
    LV_ASSERT_MALLOC(dsc);
    if(dsc == NULL) return NULL;

    lv_indev_t * indev = lv_indev_create();
    LV_ASSERT_MALLOC(indev);
    if(indev == NULL) {
        lv_free(dsc);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, sdl_keyboard_read);
    lv_indev_set_driver_data(indev, dsc);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_add_event_cb(indev, release_indev_cb, LV_EVENT_DELETE, indev);
    SDL_StartTextInput();
    return indev;
}

                       
                     
                        

                                                       
static void send_key_item_event(lv_sdl_keyboard_t * dev,
                                uint32_t codepoint,
                                const char * utf8, size_t utf8_len,
                                int key_state)
{
    struct key_item * elm = (struct key_item *)calloc(1, sizeof(struct key_item));
    if(elm == NULL) return;

    elm->key_code  = dev->cur_scancode;
    elm->keysym    = dev->cur_keysym;
    elm->codepoint = codepoint;
    elm->mods      = dev->cur_mods;
    elm->key_state = key_state;                               
    if(elm->key_code == KEY_ESC) {
        LVGL_HOME_KEY_FLAGE = elm->key_state;
    }

                  
    if(dev->cur_sym_name[0]) {
        strncpy(elm->sym_name, dev->cur_sym_name, sizeof(elm->sym_name) - 1);
        elm->sym_name[sizeof(elm->sym_name) - 1] = '\0';
    }

              
    if(utf8 && utf8_len) {
        size_t n = utf8_len < sizeof(elm->utf8) - 1 ? utf8_len : sizeof(elm->utf8) - 1;
        memcpy(elm->utf8, utf8, n);
        elm->utf8[n] = '\0';
    }

    elm->flage = 1;                                                                       

    lv_obj_t * root = lv_screen_active();
    if(root) {
        lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, elm);
    }

                                                    
    free(elm);
}

static void sdl_keyboard_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    lv_sdl_keyboard_t * dev = lv_indev_get_driver_data(indev);
    const size_t len = lv_strlen(dev->buf);
    data->continue_reading = false;

                      
    if(dev->dummy_read) {
        dev->dummy_read = false;
        data->state = LV_INDEV_STATE_RELEASED;
                                                   
        data->key = dev->last_codepoint;

        dev->cur_valid = false;

                          
        dev->last_codepoint = 0;
        dev->last_utf8[0]   = '\0';
        dev->last_utf8_len  = 0;
    }
                      
    else if(len > 0) {
        dev->dummy_read = true;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = 0;

        uint32_t utf8_len = lv_text_encoded_size(dev->buf);
        if(utf8_len == 0) utf8_len = 1;

                                               
        uint32_t i = 0;
        uint32_t codepoint = lv_text_encoded_next(dev->buf, &i);
        if(codepoint == 0) {
                                                                        
            codepoint = (uint8_t)dev->buf[0];
        }
        data->key = codepoint;

                                                           
        dev->last_codepoint = codepoint;
        size_t n = utf8_len < sizeof(dev->last_utf8) - 1 ? utf8_len
                                                        : sizeof(dev->last_utf8) - 1;
        memcpy(dev->last_utf8, dev->buf, n);
        dev->last_utf8[n]  = '\0';
        dev->last_utf8_len = n;

                                
        send_key_item_event(dev, codepoint, dev->buf, utf8_len,
                            dev->cur_key_state == KBD_KEY_REPEATED
                                ? KBD_KEY_REPEATED : KBD_KEY_PRESSED);

                                         
        lv_memmove(dev->buf, dev->buf + utf8_len, len - utf8_len + 1);
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void release_indev_cb(lv_event_t * e)
{
    lv_indev_t * indev = (lv_indev_t *)lv_event_get_user_data(e);
    lv_sdl_keyboard_t * dev = lv_indev_get_driver_data(indev);
    if(dev) {
        lv_indev_set_driver_data(indev, NULL);
        lv_indev_set_read_cb(indev, NULL);
        lv_free(dev);
        LV_LOG_INFO("done");
    }
}

void lv_sdl_keyboard_handler(SDL_Event * event)
{
    uint32_t win_id = UINT32_MAX;
    switch(event->type) {
        case SDL_KEYDOWN:     win_id = event->key.windowID;  break;
        case SDL_KEYUP:       win_id = event->key.windowID;  break;
        case SDL_TEXTINPUT:   win_id = event->text.windowID; break;
        default: return;
    }

    lv_display_t * disp = lv_sdl_get_disp_from_win_id(win_id);

    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_read_cb(indev) == sdl_keyboard_read) {
            if(disp == NULL || lv_indev_get_display(indev) == disp) break;
        }
        indev = lv_indev_get_next(indev);
    }
    if(indev == NULL) return;

    lv_sdl_keyboard_t * dsc = lv_indev_get_driver_data(indev);

    switch(event->type) {
        case SDL_KEYUP: {
            SDL_Keycode  sym = event->key.keysym.sym;
            SDL_Scancode sc  = event->key.keysym.scancode;
            Uint16       md  = event->key.keysym.mod;
            dsc->cur_scancode = sdl_key_to_input_key(sym, sc);
            dsc->cur_keysym = (uint32_t)sym;
            dsc->cur_mods = 0;
            if(md & KMOD_SHIFT) dsc->cur_mods |= KBD_MOD_SHIFT;
            if(md & KMOD_CTRL) dsc->cur_mods |= KBD_MOD_CTRL;
            if(md & KMOD_ALT) dsc->cur_mods |= KBD_MOD_ALT;
            if(md & KMOD_GUI) dsc->cur_mods |= KBD_MOD_LOGO;

            const char * kname = SDL_GetKeyName(sym);
            if(kname) {
                strncpy(dsc->cur_sym_name, kname, sizeof(dsc->cur_sym_name) - 1);
                dsc->cur_sym_name[sizeof(dsc->cur_sym_name) - 1] = '\0';
            }
            else {
                dsc->cur_sym_name[0] = '\0';
            }

            char utf8[2] = {0, 0};
            uint32_t codepoint = 0;
            if(sym >= 0x20 && sym < 0x7f) {
                codepoint = (uint32_t)sym;
                utf8[0] = (char)sym;
            }
            send_key_item_event(dsc, codepoint,
                                codepoint ? utf8 : NULL,
                                codepoint ? 1U : 0U,
                                KBD_KEY_RELEASED);
            dsc->cur_valid = false;
            return;
        }

        case SDL_KEYDOWN: {
            SDL_Keycode   sym = event->key.keysym.sym;
            SDL_Scancode  sc  = event->key.keysym.scancode;
            Uint16        md  = event->key.keysym.mod;

                                             
            dsc->cur_scancode = sdl_key_to_input_key(sym, sc);
            dsc->cur_keysym   = (uint32_t)sym;
            dsc->cur_mods     = 0;
            dsc->cur_key_state = event->key.repeat
                                     ? KBD_KEY_REPEATED : KBD_KEY_PRESSED;
            if(md & KMOD_SHIFT) dsc->cur_mods |= KBD_MOD_SHIFT;
            if(md & KMOD_CTRL)  dsc->cur_mods |= KBD_MOD_CTRL;
            if(md & KMOD_ALT)   dsc->cur_mods |= KBD_MOD_ALT;
            if(md & KMOD_GUI)   dsc->cur_mods |= KBD_MOD_LOGO;

            const char * kname = SDL_GetKeyName(sym);
            if(kname) {
                strncpy(dsc->cur_sym_name, kname, sizeof(dsc->cur_sym_name) - 1);
                dsc->cur_sym_name[sizeof(dsc->cur_sym_name) - 1] = '\0';
            }
            else {
                dsc->cur_sym_name[0] = '\0';
            }
            dsc->cur_valid = true;

                                       
            const uint32_t ctrl_key = keycode_to_ctrl_key(sym);
            if(ctrl_key == '\0') {
                if((md & (KMOD_CTRL | KMOD_GUI)) && sym == SDLK_v) {
                    const size_t blen = lv_strlen(dsc->buf);
                    if(blen < KEYBOARD_BUFFER_SIZE - 1) {
                        dsc->buf[blen] = 'v';
                        dsc->buf[blen + 1] = '\0';
                    }
                }
                else if((md & (KMOD_CTRL | KMOD_ALT)) &&
                        (sym == SDLK_COMMA || sym == SDLK_LESS ||
                         sym == SDLK_PERIOD || sym == SDLK_GREATER)) {
                    const size_t blen = lv_strlen(dsc->buf);
                    if(blen < KEYBOARD_BUFFER_SIZE - 1) {
                        dsc->buf[blen] =
                            (sym == SDLK_COMMA || sym == SDLK_LESS) ? '<' : '>';
                        dsc->buf[blen + 1] = '\0';
                    }
                }
                else {
                    return;                                                 
                }
            }
            else {
                const size_t blen = lv_strlen(dsc->buf);
                if(blen < KEYBOARD_BUFFER_SIZE - 1) {
                    dsc->buf[blen] = ctrl_key;
                    dsc->buf[blen + 1] = '\0';
                }
            }
            break;
        }

        case SDL_TEXTINPUT: {
                                                                                          
            if(!dsc->cur_valid) {
                dsc->cur_scancode = 0;
                dsc->cur_keysym   = 0;
                dsc->cur_mods     = 0;
                dsc->cur_key_state = KBD_KEY_PRESSED;
                dsc->cur_sym_name[0] = '\0';
                dsc->cur_valid = true;
            }
            const size_t total = lv_strlen(dsc->buf) + lv_strlen(event->text.text);
            if(total < KEYBOARD_BUFFER_SIZE - 1)
                lv_strcat(dsc->buf, event->text.text);
            break;
        }

        default: break;
    }

    size_t len = lv_strlen(dsc->buf);
    while(len) {
        lv_indev_read(indev);                                              
        lv_indev_read(indev);                                                     
        len--;
    }
}

static uint32_t keycode_to_ctrl_key(SDL_Keycode sdl_key)
{
    switch(sdl_key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS:    return LV_KEY_RIGHT;
        case SDLK_LEFT:
        case SDLK_KP_MINUS:   return LV_KEY_LEFT;
        case SDLK_UP:         return LV_KEY_UP;
        case SDLK_DOWN:       return LV_KEY_DOWN;
        case SDLK_ESCAPE:     return LV_KEY_ESC;
        case SDLK_BACKSPACE:  return LV_KEY_BACKSPACE;
        case SDLK_DELETE:     return LV_KEY_DEL;
        case SDLK_KP_ENTER:
        case '\r':            return LV_KEY_ENTER;
        case SDLK_TAB:        return 15;
        case SDLK_PAGEDOWN:   return LV_KEY_NEXT;
        case SDLK_PAGEUP:     return LV_KEY_PREV;
        case SDLK_HOME:       return LV_KEY_HOME;
        case SDLK_END:        return LV_KEY_END;
            default:              return '\0';
    }
}

static uint32_t sdl_key_to_input_key(SDL_Keycode sdl_key, SDL_Scancode scancode)
{
    switch(sdl_key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS:   return KEY_RIGHT;
        case SDLK_LEFT:
        case SDLK_KP_MINUS:  return KEY_LEFT;
        case SDLK_UP:        return KEY_UP;
        case SDLK_DOWN:      return KEY_DOWN;
        case SDLK_ESCAPE:    return KEY_ESC;
        case SDLK_BACKSPACE: return KEY_BACKSPACE;
        case SDLK_DELETE:    return KEY_DELETE;
        case SDLK_KP_ENTER:
        case SDLK_RETURN:    return KEY_ENTER;
        case SDLK_TAB:       return KEY_TAB;
        case SDLK_PAGEDOWN:  return KEY_NEXT;
        case SDLK_PAGEUP:    return KEY_PREVIOUS;
        case SDLK_HOME:      return KEY_HOME;
        case SDLK_END:       return KEY_END;
        default:             return (uint32_t)scancode;
    }
}

#endif
