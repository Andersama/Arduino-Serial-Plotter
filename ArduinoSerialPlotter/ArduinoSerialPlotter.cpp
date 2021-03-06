// ArduinoSerialPlotter.cpp : Defines the entry point for the application.
//
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include "ArduinoSerialPlotter.h"
#include "real_vector.h"

#include "SerialClass.h" // Library described above
#include <charconv>
#include <fmt/core.h>
#include <fmt/format.h>
#include <math.h>
#include <ostream>
#include <stdio.h>
#include <string>
#include <tchar.h>
#include <unordered_map>

#include "simdjson.h"
using namespace simdjson;

#include <GL/glew.h>
#include <GLFW/glfw3.h>
// chart plotter api allows up to 6 colors, we'll do 8
// #define NK_CHART_MAX_SLOT 8
#define NK_PRIVATE true
#define NK_SINGLE_FILE true
#define NK_API
#define NK_LIB
#define NK_MEMCPY std::memcpy
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL4_IMPLEMENTATION
#define NK_KEYSTATE_BASED_INPUT
#include "nuklear.h"
#include "nuklear_glfw_gl4.h"

NK_API void nk_noop() {}
// x,y -> 4 per float, 8 per x,y ergo
#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER (MAX_VERTEX_BUFFER / 4)
//#define MAX_VERTEX_BUFFER 4096 * 1024
//#define MAX_ELEMENT_BUFFER 1024 * 1024
//#define MAX_VERTEX_BUFFER 512 * 1024
//#define MAX_ELEMENT_BUFFER 128 * 1024

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// pcg-random.org
typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_random_t;

uint32_t pcg32_random_r(pcg32_random_t *rng) {
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    // Calculate output function (XSH RR), uses old state for max ILP
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

std::unordered_map<std::string_view, nk_color> color_map = {
    {"red", nk_color{255, 0, 0, 255}},      {"green", nk_color{0, 255, 0, 255}},
    {"blue", nk_color{0, 0, 255, 255}},     {"orange", nk_color{255, 153, 51, 255}},
    {"yellow", nk_color{255, 255, 0, 255}}, {"pink", nk_color{255, 51, 204, 255}},
    {"purple", nk_color{172, 0, 230, 255}}, {"cyan", nk_color{0, 255, 255, 255}},
    {"white", nk_color{255, 255, 255, 255}}};

nk_color get_color(std::string_view v) {
    auto it = color_map.find(v);
    if (it != color_map.end()) {
        return it->second;
    } else {
        // return a consistant color for the word by using the hash
        size_t h = color_map.hash_function()(v);
        size_t idx = h % color_map.size();
        return std::next(color_map.begin(), idx)->second;
    }
}

template <typename T> struct smooth_data {
    T value;
    T lerp_v;
    T get_delta(T v) { return v - value; }
    T get_next_smooth(T v) {
        // smooth logarithmic curve
        value = std::lerp(value, v, lerp_v);
        return value;
    }
    T get_next_smooth_lower(T v) {
        if (v < value)
            return value = v;
        else
            return value = std::lerp(value, v, lerp_v);
    }
    T get_next_smooth_upper(T v) {
        if (v > value)
            return value = v;
        else
            return value = std::lerp(value, v, lerp_v);
    }
};

std::string example_json = R"raw({"t": 5649,
  "ng": 5,
  "lu": 4086,
  "g": [
        {
          "t": "Temps",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "Low Temp",
                "High Temp",
                "Smooth Avg"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                80.48750305,
                80.82499694,
                80.65625
          ]
        },
        {
          "t": "Pump Status",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "Pump",
                "Thermal",
                "Light"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                0,
                0,
                0
          ]
        },
        {
          "t": "Lux",
          "xvy": 0,
          "pd": 60,
          "sz": 4,
          "l": [
                "Value",
                "Smooth",
                "Low",
                "High"
          ],
          "c": [
                "green",
                "orange",
                "cyan",
                "yellow"
          ],
          "d": [
                2274.62939453,
                2277.45947265,
                4050,
                4500
          ]
        },
        {
          "t": "Temp Diff",
          "xvy": 0,
          "pd": 60,
          "sz": 4,
          "l": [
                "dFarenheit",
                "sum",
                "Low",
                "High"
          ],
          "c": [
                "green",
                "orange",
                "cyan",
                "yellow"
          ],
          "d": [
                0,
                0,
                0.5,
                10
          ]
        },
        {
          "t": "Power",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "watts (est. ligth)",
                "watts (1.9~gpm)",
                "watts (1gpm)"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                181.78063964,
                114.88922882,
                59.35943603
          ]
        }
  ]
}                            
                              
                               
0123456789
0123456789
0123456789
0123456789

0123456789
0123456789
0123456789
0123456789
                  )raw";

std::string mangled_example_json = R"raw(        {
          "t": "Temp Diff",
          "xvy": 0,
          "pd": 60,
          "sz": 4,
          "l": [
                "dFarenheit",
                "sum",
                "Low",
                "High"
          ],
          "c": [
                "green",
                "orange",
                "cyan",
                "yellow"
          ],
          "d": [
                0,
                0,
                0.5,
                10
          ]
        },
        {
          "t": "Power",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "watts (est. ligth)",
                "watts (1.9~gpm)",
                "watts (1gpm)"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                181.78063964,
                114.88922882,
                59.35943603
          ]
        }
  ]
}   !@#$%^&*()`;'?><.0123456789some_errant_data   {"t": 5649,
  "ng": 5,
  "lu": 4086,
  "g": [
        {
          "t": "Temps",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "Low Temp",
                "High Temp",
                "Smooth Avg"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                80.48750305,
                80.82499694,
                80.65625
          ]
        },
        {
          "t": "Pump Status",
          "xvy": 0,
          "pd": 60,
          "sz": 3,
          "l": [
                "Pump",
                "Thermal",
                "Light"
          ],
          "c": [
                "green",
                "orange",
                "cyan"
          ],
          "d": [
                0,
                0,
                0
          ]
        },
        {
          "t": "Lux",
          "xvy": 0,
          "pd": 60,
          "sz": 4,
          "l": [
                "Value",
                "Smooth",
                "Low",
                "High"
          ],
          "c": [
                "green",
                "orange",
                "cyan",
                "yellow"
          ],
          "d": [
                2274.62939453,
                2277.45947265,
                4050,
                4500
          ]
        },
                      
                              
                               
                  )raw";

static void error_callback(int e, const char *d) { printf("Error %d: %s\n", e, d); }

std::string cout_buffer;
template <typename... Args> void print_out(Args &&...args) {
    fmt::format_to(std::back_inserter(cout_buffer), std::forward<Args>(args)...);
    std::cout << cout_buffer;
    cout_buffer.clear();
}

template <typename... Args> void format_out(Args &&...args) {
    fmt::format_to(std::back_inserter(cout_buffer), std::forward<Args>(args)...);
    std::cout << cout_buffer;
    // cout_buffer.clear();
}

struct graph_t {
    real::vector<real::vector<struct nk_vec2>> values;
    // x (evens), y (odds)
    real::vector<float> points;
    real::vector<std::string> labels;
    real::vector<nk_color> colors;

    smooth_data<float> upper_value;
    smooth_data<float> lower_value;

    size_t limit = 60;
    size_t slots = 0;
    std::string title;
};

size_t get_lsb_set(unsigned int v) noexcept {
    // find the number of trailing zeros in 32-bit v
    int r; // result goes here
    static const int MultiplyDeBruijnBitPosition[32] = {0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
                                                        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};
    r = MultiplyDeBruijnBitPosition[((uint32_t)((v & -v) * 0x077CB531U)) >> 27];
    return r;
}

void nk_plot_multi(struct nk_context *ctx, enum nk_chart_type type, const float **values, int count, int offset,
                   int slots) {
    int i = 0;
    int j = 0;
    float min_value;
    float max_value;

    NK_ASSERT(ctx);
    NK_ASSERT(values);
    if (!ctx || !values || !count || !slots)
        return;

    min_value = values[0][offset];
    max_value = values[0][offset];
    for (j = 0; j < slots; ++j) {
        for (i = 0; i < count; ++i) {
            min_value = NK_MIN(values[j][i + offset], min_value);
            max_value = NK_MAX(values[j][i + offset], max_value);
        }
    }

    if (nk_chart_begin(ctx, type, count, min_value, max_value)) {
        for (j = 0; j < slots; ++j) {
            for (i = 0; i < count; ++i) {
                nk_chart_push_slot(ctx, values[j][i + offset], j);
            }
        }
        nk_chart_end(ctx);
    }
}

nk_flags nk_chart_draw_yticks(struct nk_context *ctx, struct nk_window *win, struct nk_chart *g, float yoffset,
                              float xoffset, float spacing, nk_flags alignment) {
    struct nk_panel *layout = win->layout;
    const struct nk_input *i = &ctx->input;
    struct nk_command_buffer *out = &win->buffer;

    nk_flags ret = 0;
    struct nk_vec2 cur;
    struct nk_rect bounds;
    struct nk_color color;
    float step;
    float range;
    float ratio;

    step = g->w / (float)g->slots[0].count;
    range = g->slots[0].max - g->slots[0].min;
    // ratio = (value - g->slots[slot].min) / range;

    float half_step = step * 0.5f;

    float h = g->h;

    const struct nk_style *style;
    struct nk_vec2 item_padding;
    struct nk_text text;

    style = &ctx->style;
    item_padding = style->text.padding;

    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = style->window.background;
    text.text = ctx->style.text.color;

    color = g->slots[0].color;
    color.a = 128;
    // cur.x = g->x + (float)(step * (float)g->slots[slot].index);
    cur.x = g->x;
    // skip a half step
    cur.y = yoffset;

    float bottom = (g->y + g->h);
    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float style_offset = bottom - (style->font->height / 2.0f);

        bounds.x = g->x + step + xoffset;
        bounds.h = style->font->height;

        // half_step + xoffset;
        // bounds.w = 20.0f;
        for (; cur.y < h; cur.y += spacing) {
            nk_stroke_line(out, g->x, bottom - cur.y, g->x + half_step, bottom - cur.y, 2.0f,
                           nk_color{255, 255, 255, 255});

            float percentage = cur.y / h;
            float value = std::lerp(g->slots[0].min, g->slots[0].max, percentage);
            char floating_point[64];
            auto label = std::to_chars(floating_point, (floating_point) + 64, value);
            *label.ptr = 0;
            size_t len = label.ptr - floating_point;
            bounds.y = style_offset - cur.y;
            // get the width so we can print w/ a solid background
            // bounds.w = style->font->width(style->font->userdata, style->font->height, (const char *)floating_point,
            // len);
            bounds.w = 30.0f;
            // write text
            nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
            nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
                           style->font);
        }
    } else { // NK_TEXT_ALIGN_RIGHT
        float style_offset = bottom - (style->font->height / 2.0f);

        float right = (g->x + g->w);
        bounds.h = style->font->height;
        bounds.w = half_step + xoffset;
        bounds.x = ((right - step) - bounds.w) - xoffset;

        for (; cur.y < h; cur.y += spacing) {
            nk_stroke_line(out, right - half_step, bottom - cur.y, right, bottom - cur.y, 2.0f,
                           nk_color{255, 255, 255, 255});

            float percentage = cur.y / h;
            float value = std::lerp(g->slots[0].min, g->slots[0].max, percentage);
            char floating_point[64];
            auto label = std::to_chars(floating_point, (floating_point) + 64, value);
            *label.ptr = 0;
            size_t len = label.ptr - floating_point;
            bounds.y = style_offset - cur.y;

            // get the width so we can print w/ a solid background
            // bounds.w = style->font->width(style->font->userdata, style->font->height, (const char *)floating_point,
            // len);
            bounds.w = 30.0f;
            // write text
            nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
            nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
                           style->font);
        }
    }

    return ret;
}

nk_flags nk_chart_draw_line(struct nk_context *ctx, struct nk_rect chart_bounds, float min_value, float max_value,
                            float value, float line_length, float line_thickness, nk_color color, nk_flags alignment) {
    // nk_tooltip(ctx, "text");
    float range = max_value - min_value;
    // ctx->current->
    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float y = (chart_bounds.y + chart_bounds.h) - ((value - min_value) / range) * chart_bounds.h;

        nk_stroke_line(&ctx->current->buffer, chart_bounds.x, y, chart_bounds.x + line_length, y, line_thickness,
                       color);
    } else if (alignment & NK_TEXT_ALIGN_TOP) {
        float x = (chart_bounds.x) + ((value - min_value) / range) * chart_bounds.w;

        nk_stroke_line(&ctx->current->buffer, x, chart_bounds.y, x, chart_bounds.y + line_length, line_thickness,
                       color);
    } else if (alignment & NK_TEXT_ALIGN_BOTTOM) {
        float x = (chart_bounds.x) + ((value - min_value) / range) * chart_bounds.w;
        float bottom = chart_bounds.y + chart_bounds.h;

        nk_stroke_line(&ctx->current->buffer, x, bottom, x, bottom - line_length, line_thickness, color);
    } else {
        float y = (chart_bounds.y + chart_bounds.h) - ((value - min_value) / range) * chart_bounds.h;
        float right = chart_bounds.x + chart_bounds.w;
        nk_stroke_line(&ctx->current->buffer, right, y, right - line_length, y, line_thickness, color);
    }

    return 0;
}

nk_flags nk_chart_draw_value(struct nk_context *ctx, struct nk_rect chart_bounds, float min_value, float max_value,
                             float value, float line_length, float line_thickness, nk_color color, nk_flags alignment,
                             nk_flags text_alignment) {
    float range = max_value - min_value;

    char text_value[64];
    auto chrs = std::to_chars(text_value, text_value + 64, value);
    *chrs.ptr = 0;
    size_t len = chrs.ptr - text_value;

    struct nk_text text;
    struct nk_vec2 item_padding;
    item_padding = (&ctx->style)->text.padding;
    // text settings
    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = (&ctx->style)->window.background;
    text.text = color;

    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float y = (chart_bounds.y + chart_bounds.h) - ((value - min_value) / range) * chart_bounds.h;

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y = y - (text.padding.y + (ctx->style.font->height) / 2.0);
        text_bounds.h = ctx->style.font->height;
        text_bounds.x += line_length;
        text_bounds.w -= 2 * line_length;

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
        // nk_draw_text(&ctx->current->buffer, , , , ctx->style.font, color, color);
    } else if (alignment & NK_TEXT_ALIGN_TOP) {
        float x = (chart_bounds.x) + ((value - min_value) / range) * chart_bounds.w;
        float text_w =
            ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, (const char *)text_value, len);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y += line_length;
        text_bounds.h -= 2 * line_length;
        text_bounds.x = x - ((text_w + (2.0f * text.padding.x)) / 2.0f);
        text_bounds.w = text_w + (2.0f * text.padding.x);

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    } else if (alignment & NK_TEXT_ALIGN_BOTTOM) {
        float x = (chart_bounds.x) + ((value - min_value) / range) * chart_bounds.w;

        float text_w =
            ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, (const char *)text_value, len);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y += line_length;
        text_bounds.h -= 2 * line_length;
        text_bounds.x = x - ((text_w + (2.0f * text.padding.x)) / 2.0f);
        text_bounds.w = text_w + (2.0f * text.padding.x);

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    } else {
        float y = (chart_bounds.y + chart_bounds.h) - ((value - min_value) / range) * chart_bounds.h;

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y = y - (ctx->style.font->height) / 2.0;
        text_bounds.h = ctx->style.font->height;
        text_bounds.x += line_length;
        text_bounds.w -= 2 * line_length;

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    }

    return 0;
}

nk_flags nk_chart_draw_line_uv(struct nk_context *ctx, struct nk_rect chart_bounds, float uv, float line_length,
                               float line_thickness, nk_color color, nk_flags alignment) {

    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float y = (chart_bounds.y + chart_bounds.h) - (uv * chart_bounds.h);

        nk_stroke_line(&ctx->current->buffer, chart_bounds.x, y, chart_bounds.x + line_length, y, line_thickness,
                       color);
    } else if (alignment & NK_TEXT_ALIGN_TOP) {
        float x = (chart_bounds.x) + (uv * chart_bounds.w);

        nk_stroke_line(&ctx->current->buffer, x, chart_bounds.y, x, chart_bounds.y + line_length, line_thickness,
                       color);
    } else if (alignment & NK_TEXT_ALIGN_BOTTOM) {
        float x = (chart_bounds.x) + (uv * chart_bounds.w);
        float bottom = chart_bounds.y + chart_bounds.h;

        nk_stroke_line(&ctx->current->buffer, x, bottom, x, bottom - line_length, line_thickness, color);
    } else {
        float y = (chart_bounds.y + chart_bounds.h) - (uv * chart_bounds.h);
        float right = chart_bounds.x + chart_bounds.w;
        nk_stroke_line(&ctx->current->buffer, right, y, right - line_length, y, line_thickness, color);
    }

    return 0;
}

nk_flags nk_chart_draw_value_uv(struct nk_context *ctx, struct nk_rect chart_bounds, float min_value, float max_value,
                                float uv, float line_length, float line_thickness, nk_color color, nk_flags alignment,
                                nk_flags text_alignment) {
    float range = max_value - min_value;
    float value = min_value + (uv * range);
    char text_value[64];
    auto chrs = std::to_chars(text_value, text_value + 64, value);
    *chrs.ptr = 0;
    size_t len = chrs.ptr - text_value;

    struct nk_text text;
    struct nk_vec2 item_padding;
    item_padding = (&ctx->style)->text.padding;
    // text settings
    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = (&ctx->style)->window.background;
    text.text = color;

    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float y = (chart_bounds.y + chart_bounds.h) - (uv * chart_bounds.h);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y = y - (text.padding.y + (ctx->style.font->height) / 2.0);
        text_bounds.h = ctx->style.font->height;
        text_bounds.x += line_length;
        text_bounds.w -= 2 * line_length;

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
        // nk_draw_text(&ctx->current->buffer, , , , ctx->style.font, color, color);
    } else if (alignment & NK_TEXT_ALIGN_TOP) {
        float x = (chart_bounds.x) + (uv * chart_bounds.w);
        float text_w =
            ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, (const char *)text_value, len);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y += line_length;
        text_bounds.h -= 2 * line_length;
        text_bounds.x = x - ((text_w + (2.0f * text.padding.x)) / 2.0f);
        text_bounds.w = text_w + (2.0f * text.padding.x);

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    } else if (alignment & NK_TEXT_ALIGN_BOTTOM) {
        float x = (chart_bounds.x) + (uv * chart_bounds.w);

        float text_w =
            ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, (const char *)text_value, len);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y += line_length;
        text_bounds.h -= 2 * line_length;
        text_bounds.x = x - ((text_w + (2.0f * text.padding.x)) / 2.0f);
        text_bounds.w = text_w + (2.0f * text.padding.x);

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    } else {
        float y = (chart_bounds.y + chart_bounds.h) - (uv * chart_bounds.h);

        struct nk_rect text_bounds = chart_bounds;
        text_bounds.y = y - (ctx->style.font->height) / 2.0;
        text_bounds.h = ctx->style.font->height;
        text_bounds.x += line_length;
        text_bounds.w -= 2 * line_length;

        nk_widget_text(&ctx->current->buffer, text_bounds, text_value, len, &text, text_alignment, ctx->style.font);
    }

    return 0;
}

std::string stream_buffer;
// returns the number of graphs to draw, 0 -> no data / no change
size_t handle_json(ondemand::parser &parser, struct nk_context *ctx, real::vector<graph_t> &graphs, const char *ptr,
                   uint32_t read_count) {
    size_t graphs_to_display = 0;
    if (read_count) {
        // make room for simdjson's scratchbuffer and the incoming data
        if (stream_buffer.capacity() < (stream_buffer.size() + read_count + (SIMDJSON_PADDING + 1)))
            stream_buffer.reserve((stream_buffer.size() + read_count) * 2 + (SIMDJSON_PADDING + 1));
        // append the data in case we had incomplete data before.
        stream_buffer.append(ptr, read_count);
        // wait for buffer to be a decent size
        if (stream_buffer.size() < 512)
            return graphs_to_display;
        // validate buffer is utf8 once
        if (!simdjson::validate_utf8(stream_buffer.data(), stream_buffer.size()))
            return graphs_to_display;

        ondemand::document graph_data;
        do {
            auto lbrace = std::find(stream_buffer.data(), stream_buffer.data() + stream_buffer.size(), '{');

            size_t dist = lbrace - stream_buffer.data();

            padded_string_view json(stream_buffer.data() + dist, stream_buffer.size() - dist,
                                    stream_buffer.capacity() - dist);
            auto error = parser.iterate(json).get(graph_data);
            size_t g = 0;
            if (!error) {
#if 1
                // bool result = true;
                std::string_view v;
                try {
                    // get the view of the json object
                    // this effectively validates that we have a complete json object (no errors) in our buffer
                    // seems like additional effort
                    
                    auto error = simdjson::to_json_string(graph_data).get(v);
                    if (error == simdjson::INCOMPLETE_ARRAY_OR_OBJECT) {
                        // wait for object to complete in a another call
                        return graphs_to_display;
                    } else if (error != simdjson::SUCCESS) {
                        // general failure move buffer over by one
                        stream_buffer.erase(stream_buffer.begin());
                        return graphs_to_display;
                    }
                    
                    // now we extract the data we need
                    /*
                    ondemand::object obj;
                    auto objerr = graph_data.get_object().get(obj);
                    if (objerr == simdjson::INCOMPLETE_ARRAY_OR_OBJECT) {
                        //nothing to parse here
                        return graphs_to_display;
                    } else if (objerr != simdjson::SUCCESS) {
                        stream_buffer.erase(stream_buffer.begin());
                        return graphs_to_display;
                    } else {
                        //ok?
                        v = {stream_buffer.data()+dist, 1}; //stream_buffer.capacity()-dist
                    }
                    */
                    
                    /*
                    ondemand::json_type t;
                    auto error = graph_data.type().get(t);
                    if (error == simdjson::INCOMPLETE_ARRAY_OR_OBJECT) {
                        // wait for object to complete in a another call
                        return graphs_to_display;
                    } else if (error != simdjson::SUCCESS) {
                        stream_buffer.erase(stream_buffer.begin());
                        return graphs_to_display;
                    }

                    if (t != ondemand::json_type::object) {
                        stream_buffer.erase(stream_buffer.begin());
                        return graphs_to_display;
                    }
                    */
                    // size_t ts;
                    // auto err = graph_data.get_object().find_field("t").get(ts);
                    size_t ts;
                    auto err = graph_data["t"].get_uint64().get(ts);
                    if (err) {
                        // could not find timestamp field
                        // erase the object we read from the stream
                        stream_buffer.erase(size_t{0}, (v.data() + v.size()) - stream_buffer.data());
                        continue;
                    } else {
                        ondemand::array graphs_array;
                        auto graphs_err = graph_data["g"].get_array().get(graphs_array);

                        if (graphs_err) {
                            // could not find the g field (graphs)
                            // erase the object we read from the stream
                            stream_buffer.erase(size_t{0}, (v.data() + v.size()) - stream_buffer.data());
                            continue; // return graphs_to_display;
                        } else {
                            for (auto graph : graphs_array) {
                                // add graph to keep track of
                                if (g >= graphs.size()) {
                                    graphs.emplace_back();
                                    // graphs[g].values.reserve(128);
                                };
                                std::string_view title;
                                if (auto title_err = graph["t"].get_string().get(title)) {

                                } else {
                                    graphs[g].title = title;
                                }

                                ondemand::array labels_array;
                                if (auto labels_err = graph["l"].get_array().get(labels_array)) {

                                } else {
                                    size_t count = 0;
                                    for (auto label : labels_array) {
                                        if (count >= graphs[g].values.size()) {
                                            graphs[g].values.emplace_back();
                                            graphs[g].values[count].reserve(graphs[g].limit);
                                            graphs[g].labels.emplace_back("");
                                            graphs[g].colors.emplace_back(ctx->style.chart.color);
                                        }

                                        auto v = label.get_string();
                                        auto vw = v.value();
                                        graphs[g].labels[count] = vw;
                                        graphs[g].colors[count] = get_color(vw);

                                        count++;
                                    }
                                }
                                // if we have a color for the index it overrides the hashed one
                                ondemand::array colors_array;
                                if (auto color_err = graph["c"].get_array().get(colors_array)) {

                                } else {
                                    size_t count = 0;
                                    for (auto color : colors_array) {
                                        if (count >= graphs[g].values.size()) {
                                            graphs[g].values.emplace_back();
                                            graphs[g].values[count].reserve(graphs[g].limit);
                                            graphs[g].labels.emplace_back("");
                                            graphs[g].colors.emplace_back(ctx->style.chart.color);
                                        }

                                        auto v = color.get_string();
                                        auto vw = v.value();
                                        graphs[g].colors[count] = get_color(vw);

                                        count++;
                                    }
                                }

                                size_t limit = 60;
                                if (auto pd_err = graph["pd"].get(limit)) {
                                    // err
                                } else {
                                    // clamp to at least 1 data point
                                    graphs[g].limit = limit > 0 ? limit : 1;
                                }

                                float mn = std::numeric_limits<float>::max();
                                float mx = std::numeric_limits<float>::min();
                                uint32_t count = 0;
                                float flt_ts = ts;

                                ondemand::array data_points;
                                if (auto d_err = graph["d"].get_array().get(data_points)) {
                                    return false;
                                } else {
                                    // go through data points;
                                    for (auto value : data_points) {
                                        if (count >= graphs[g].values.size()) {
                                            graphs[g].values.emplace_back();
                                            graphs[g].values[count].reserve(graphs[g].limit);
                                            graphs[g].labels.emplace_back("");
                                            graphs[g].colors.emplace_back(ctx->style.chart.color);
                                        }

                                        struct nk_vec2 point;
                                        point.x = flt_ts;
                                        point.y = (float)(double)value;
                                        graphs[g].values[count].emplace_back(point);
                                        //
                                        if (graphs[g].values[count].size() > graphs[g].limit) {
                                            graphs[g].values[count].erase(graphs[g].values[count].begin());
                                        }

                                        count++;
                                    }
                                    graphs[g].slots = count;
                                    g++;
                                }
                            }
                            // send the object out to console, make room from the start
                            if (v.size() > (cout_buffer.capacity() - cout_buffer.size())) {
                                if (v.size() > cout_buffer.capacity())
                                    cout_buffer.reserve(cout_buffer.capacity() * 2);
                                cout_buffer.erase(size_t{0}, v.size());
                            }
                            // fmt::format_to(std::back_inserter(cout_buffer), "{}", v);
                            cout_buffer.append(v);
                            // erase whatever we just read
                            stream_buffer.erase(size_t{0}, (v.data() + v.size()) - stream_buffer.data());
                        }
                    }
                } catch (const std::exception &err) {
                    cout_buffer.clear();
                    fmt::format_to(std::back_inserter(cout_buffer), "{}\n\n{}", err.what(), v);
                    // format_out("{}\n\n", err.what(), v); // shouldn't allocate in a catch block
                    g = 0;
                    stream_buffer.erase(stream_buffer.begin());
                    return graphs_to_display;
                }
                graphs_to_display = g > 0 ? g : graphs_to_display;
#endif
            } else {
                stream_buffer.erase(size_t{0}, dist ? dist : size_t{1});
                // stream_buffer.erase(stream_buffer.begin());
                return graphs_to_display = 0;
            }

        } while (stream_buffer.size() >= 512);
        return graphs_to_display;
    } else {
        return graphs_to_display;
    }
}

void clear_data(real::vector<graph_t> &graphs) {
    for (size_t i = 0; i < graphs.size(); i++) {
        for (size_t s = 0; s < graphs[s].values.size(); s++)
            graphs[i].values[s].clear();
        graphs[i].colors.clear();
        graphs[i].title.clear();
        graphs[i].points.clear();

        graphs[i].upper_value.value = 0.0f;
        graphs[i].lower_value.value = 0.0f;
    }
}

void nk_scroll(struct nk_context *ctx, float v) {
    struct nk_window *win = ctx->current;
    win->edit.scrollbar.y = v;
}

int main(int argc, char *argv[]) {
    pcg32_random_t rng;
    rng.inc = (ptrdiff_t)&rng;
    pcg32_random_r(&rng);
    rng.state = std::chrono::steady_clock::now().time_since_epoch().count();
    pcg32_random_r(&rng);

    simdjson::ondemand::parser parser;

    // pcg32_random_r
    real::vector<graph_t> graphs;
    graphs.reserve(32);

    cout_buffer.reserve(1024);
    stream_buffer.reserve(4096);
    /*GUI THINGS*/
    /* Platform */
    static GLFWwindow *win;
    int width = 0, height = 0;
    struct nk_context *ctx;
    struct nk_colorf bg;
    struct nk_image img;

    uint32_t window_width = 1920;
    uint32_t window_height = 1080;

    int graph_width = 500;
    int graph_height = 500;
    int antialiasing = true;

    /* GLFW */
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        fprintf(stdout, "[GFLW] failed to init!\n");
        exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    win = glfwCreateWindow(window_width, window_height, "Serial Plotter", NULL, NULL);
    glfwMakeContextCurrent(win);
    glfwGetWindowSize(win, &width, &height);

    /* OpenGL */
    glViewport(0, 0, width, height);
    glewExperimental = 1;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to setup GLEW\n");
        exit(1);
    }

    ctx = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);

    // load fonts
    {
        struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&atlas);
        /*struct nk_font *droid = nk_font_atlas_add_from_file(atlas,
         * "../../../extra_font/DroidSans.ttf", 14, 0);*/
        /*struct nk_font *roboto = nk_font_atlas_add_from_file(atlas,
         * "../../../extra_font/Roboto-Regular.ttf", 14, 0);*/
        /*struct nk_font *future = nk_font_atlas_add_from_file(atlas,
         * "../../../extra_font/kenvector_future_thin.ttf", 13, 0);*/
        struct nk_font *clean = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyClean.ttf", 12, 0);
        /*struct nk_font *tiny = nk_font_atlas_add_from_file(atlas,
         * "../../../extra_font/ProggyTiny.ttf", 10, 0);*/
        /*struct nk_font *cousine = nk_font_atlas_add_from_file(atlas,
         * "../../../extra_font/Cousine-Regular.ttf", 13, 0);*/
        nk_glfw3_font_stash_end();
        if (clean)
            nk_style_set_font(ctx, &clean->handle);
        /*nk_style_load_all_cursors(ctx, atlas->cursors);*/
        /*nk_style_set_font(ctx, &droid->handle);*/
    }

    int xticks = 4;
    int yticks = 4;

    size_t mx_width = 1024;
    size_t alloc_width = 1024 + SIMDJSON_PADDING;
    char *ptr = (char *)calloc(alloc_width, 1);
    size_t sz = _msize(ptr);
    sz = sz > alloc_width ? sz : alloc_width;

    char *edit_ptr = (char *)calloc(alloc_width, 1);
    size_t edit_sz = _msize(edit_ptr);
    edit_sz = edit_sz > alloc_width ? edit_sz : alloc_width;

    char *txtedit = (char *)calloc(alloc_width, 1);
    size_t txtedit_sz = _msize(txtedit);
    txtedit_sz = txtedit_sz > alloc_width ? txtedit_sz : alloc_width;
    int txtedit_len[2] = {0, 0};

    char *compath = (char *)calloc(128, 1);
    size_t compath_sz = 128;
    // don't connect
    Serial SerialPort{};

    auto SerialPorts = SerialPort.ListAvailable();
    // go to fill in first available port
    for (size_t i = 0; i < SerialPorts.size(); i++) {
        if (SerialPorts[i]) {
            size_t port_num = get_lsb_set(SerialPorts[i]);
            auto result = std::to_chars(txtedit, (txtedit + txtedit_sz), port_num);
            *result.ptr = 0;
            txtedit_len[0] = result.ptr - txtedit;
            break;
        }
    }

    int read_count = 0;
    int edit_count = 0;
    // make sure we 0
    ptr[0] = 0;
    char *bufend = ptr;
    size_t i = 0;

    std::string comport_path;
    std::string graph_title;
    std::string edit_string;

    size_t last_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    size_t last_ok_timestamp = last_timestamp;

    int demo_mode = true;
    int data_auto_scroll = true;
    int example_json_mode = false;

    int baud_rate = 115200;

    size_t graphs_to_display = 0;

    float zoom_factor = 0.20;
    float zoom_rate = 0.01f;
    float line_width = 1.0f;
    /* Main Loop */
    struct nk_rect window_bounds = nk_rect(0, 0, width, height);

    /* int fps_limit = glfwGetVideoMode(glfwGetPrimaryMonitor())->refreshRate; */
    int fps_cap = true;
    int vsync = true;

    int fps_delay = 16;

    const size_t bytes_per_second = baud_rate / 8;
    const size_t full_buffer = (mx_width - (2 * SIMDJSON_PADDING));
    const size_t ns_per_second = 1'000'000'000;
    const size_t ms_per_ns = 1'000'000;

    size_t baud_delay = ((full_buffer * ns_per_second) / bytes_per_second) + (2 * ms_per_ns);

    size_t frame_rate_delay = 33'000'000;
    size_t delay = 33'000'000;
    delay = NK_MIN(baud_delay, frame_rate_delay);
    /* Turn on VSYNC */
    glfwSwapInterval(vsync);
    size_t previous_timestamp = 0;
    while (!glfwWindowShouldClose(win)) {
        /* Input */
        glfwPollEvents();
        nk_glfw3_new_frame();
        size_t vw_height = graphs.size() * 240 + 200;
        /* Do timestamp things */
        size_t current_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        size_t timestamp_diff = current_timestamp - previous_timestamp;
        previous_timestamp = current_timestamp;

        const struct nk_rect bounds = nk_rect(0, 0, width, height);

        if (nk_begin(ctx, "Serial Plotter", bounds, NK_WINDOW_BORDER)) {
            if (nk_tree_push_hashed(ctx, NK_TREE_TAB, "Options", nk_collapse_states::NK_MAXIMIZED, "_", 1, __LINE__)) {
                nk_layout_row_dynamic(ctx, 30, 4);

                nk_label(ctx, "COM Port:", NK_TEXT_LEFT);
                nk_edit_string(ctx, NK_EDIT_SIMPLE, txtedit, txtedit_len, 4, nk_filter_decimal);

                nk_property_int(ctx, "Baud", 9600, &baud_rate, INT_MAX, 1, 1.0f);

                if (SerialPort.IsConnected()) {
                    if (demo_mode) { // if we were in demo mode clear data
                        clear_data(graphs);
                        graphs_to_display = 0;
                    }
                    demo_mode = false;
                    if (nk_button_label(ctx, "Disconnect")) {
                        SerialPort.Disconnect();
                    }
                } else {
                    if (nk_button_label(ctx, "Connect")) {
                        comport_path.clear();
                        fmt::format_to(std::back_inserter(comport_path), std::string_view{"\\\\.\\COM{}"},
                                       std::string_view{txtedit, (size_t)txtedit_len[0]});

                        // print_out(std::string_view{"attempting to connect to {}...\n"}, comport_path);
                        fmt::format_to(std::back_inserter(cout_buffer), "attempting to connect to {}...", comport_path);

                        uint32_t port_num = 0;
                        std::from_chars(txtedit, (txtedit + ((size_t)txtedit_len)), port_num, 10);
                        int result = SerialPort.Connect(port_num, false, baud_rate);
                        if (result == 0)
                            result = SerialPort.Connect(comport_path.data(), false, baud_rate);
                        // print_out(std::string_view{"{}"}, result != 0 ? std::string_view{"success!"} :
                        // std::string_view{"failed!"});
                        fmt::format_to(std::back_inserter(cout_buffer), std::string_view{"{}"},
                                       result != 0 ? std::string_view{"success!"} : std::string_view{"failed!"});

                        if (result) {
                            const size_t bytes_per_second = baud_rate / 8;
                            const size_t full_buffer = (mx_width - (2 * SIMDJSON_PADDING));
                            const size_t ns_per_second = 1'000'000'000;
                            const size_t ns_per_ms = 1'000'000;

                            size_t baud_delay = ((full_buffer * ns_per_second) / bytes_per_second) + (2 * ns_per_ms);
                            delay = baud_delay;
                        }

                        if (result)
                            demo_mode = false;
                    }
                }

                if (nk_tree_push_hashed(ctx, NK_TREE_TAB, "Gui", nk_collapse_states::NK_MINIMIZED, "_", 1, __LINE__)) {
                    nk_layout_row_dynamic(ctx, 30, 2);
                    
                    nk_label(ctx, "Marks (x-axis)", NK_TEXT_ALIGN_LEFT);
                    nk_slider_int(ctx, 2, &xticks, 20, 1);
                    nk_label(ctx, "Marks (y-axis)", NK_TEXT_ALIGN_LEFT);
                    nk_slider_int(ctx, 2, &yticks, 20, 1);
                    
                    
                    //nk_property_int(ctx, "Marks (x-axis)", 1, &xticks, 10, 1, 0.1f);
                    //nk_property_int(ctx, "Marks (y-axis)", 1, &yticks, 10, 1, 0.1f);
                    
                    nk_property_int(ctx, "Width", 100, &graph_width, 0xffff, 1, 1.0f);
                    nk_property_int(ctx, "Height", 100, &graph_height, 0xffff, 1, 1.0f);
                    // nk_label(ctx, "Zoom: ", NK_TEXT_LEFT);
                    // nk_slider_float(ctx, 0.0f, &zoom_factor, 1.5f, 0.001f);
                    nk_property_float(ctx, "Zoom", 0.0, &zoom_factor, 1.5f, 0.01f, 0.01f);
                    nk_property_float(ctx, "Rate", 0.0, &zoom_rate, 1.0, 0.0001f, 0.0001f);

                    nk_property_float(ctx, "Line Width", 1.0f, &line_width, 50.0f, 0.5f, 0.5f);
                    //pretend we have an extra widget to fill the space
                    struct nk_rect b;
                    nk_widget(&b, ctx);
                    nk_layout_row_dynamic(ctx, 30, 1);
                    //nk_widget(ctx);
                    nk_checkbox_label(ctx, "Demo", &demo_mode);
                    nk_checkbox_label(ctx, "Random/Json", &example_json_mode);
                    nk_checkbox_label(ctx, "Anti-Aliasing", &antialiasing);

                    nk_checkbox_label(ctx, "VSync", &vsync);
                    //nk_checkbox_label(ctx, "FPS Cap", &fps_cap);
                    //nk_property_int(ctx, "FPS Limit", 24, &fps_limit, 200, 1, 1.0f);
                    //fps_delay = fps_cap ? (1000 / fps_limit) : (1000 / 2000);
                    /* Update VSync */
                    glfwSwapInterval(vsync);
                    
                    nk_tree_pop(ctx);
                }

                if (nk_tree_push_hashed(ctx, NK_TREE_TAB, "Data", nk_collapse_states::NK_MINIMIZED, "_", 1, __LINE__)) {
                    nk_layout_row_dynamic(ctx, 30, 2);
                    nk_label(ctx, "Data:", NK_TEXT_LEFT);
                    nk_checkbox_label(ctx, "Autoscroll", &data_auto_scroll);

                    int len = 2048 < cout_buffer.size() ? 2048 : cout_buffer.size();
                    nk_layout_row_dynamic(ctx, 278, 1);
                    if (data_auto_scroll) {
                        nk_edit_focus(ctx, ctx->current->edit.mode);
                        // make sure we're absolutely going to scroll to the end
                        nk_scroll(ctx, (&ctx->style)->font->height * len);
                    }
                    nk_edit_string(ctx, (NK_EDIT_BOX), (cout_buffer.data() + cout_buffer.size()) - len, &len, len,
                                   nk_filter_default);
                    nk_tree_pop(ctx);
                }

                if (nk_tree_push_hashed(ctx, NK_TREE_TAB, "Input", nk_collapse_states::NK_MINIMIZED, "_", 1,
                                        __LINE__)) {
                    nk_layout_row_dynamic(ctx, 30, 2);
                    nk_label(ctx, "Data:", NK_TEXT_LEFT);

                    if (nk_button_label(ctx, "Send")) {
                        // make sure we have padding for simdjson
                        edit_string.reserve(edit_string.size() + (2 * SIMDJSON_PADDING));
                        size_t g = handle_json(parser, ctx, graphs, edit_string.data(), edit_string.size());

                        graphs_to_display = (g > 0 && g != graphs_to_display) ? g : graphs_to_display;

                        edit_string.clear();
                        edit_count = 0;
                    }

                    nk_layout_row_dynamic(ctx, 278, 1);

                    // get the area for the next widget
                    struct nk_rect edit_bounds = nk_widget_bounds(ctx);
                    // check if the user is doing something with the keyboard
                    bool any_key = ctx->input.keyboard.text_len > 0;
                    if (any_key) {
                        edit_string.reserve(edit_string.size() + ctx->input.keyboard.text_len);
                    }

                    if (nk_input_is_mouse_hovering_rect(&ctx->input, edit_bounds) &&
                        nk_input_is_key_down(&ctx->input, nk_keys::NK_KEY_PASTE)) {
                        const char *clipboard = glfwGetClipboardString(glfw.win);
                        edit_string.reserve(edit_string.size() + strlen(clipboard));
                    }

                    edit_count = edit_string.size();
                    nk_edit_string(ctx, NK_EDIT_BOX, edit_string.data(), &edit_count, edit_string.capacity(),
                                   nk_filter_default);
                    // change data to fit
                    edit_string.assign(edit_string.data(), edit_string.data() + edit_count);

                    nk_tree_pop(ctx);
                }
                nk_layout_row_dynamic(ctx, 30, 5);
                char recieved[64] = "timestamp: ";
                auto chrs = std::to_chars(recieved + 11, recieved + 64, last_ok_timestamp);
                *chrs.ptr = 0;
                nk_label(ctx, recieved, NK_TEXT_LEFT);

                char recieved2[64] = "last: ";
                chrs = std::to_chars(recieved2 + 6, recieved2 + 64, last_timestamp);
                *chrs.ptr = 0;
                nk_label(ctx, recieved2, NK_TEXT_LEFT);

                char delay_text[64] = "delay (ms): ";
                chrs = std::to_chars(delay_text + 12, delay_text + 64, delay / 1000000);
                *chrs.ptr = 0;
                nk_label(ctx, delay_text, NK_TEXT_LEFT);

                char delay_text2[64] = "delay (ns): ";
                chrs = std::to_chars(delay_text2 + 12, delay_text2 + 64, delay);
                *chrs.ptr = 0;
                nk_label(ctx, delay_text2, NK_TEXT_LEFT);

                float fps = 1.0f / ((double)timestamp_diff / (double)ns_per_second);
                char fps_text[64] = "fps: ";
                chrs = std::to_chars(fps_text + 5, fps_text + 64, fps, std::chars_format::general, 3);
                *chrs.ptr = 0;
                nk_label(ctx, fps_text, NK_TEXT_LEFT);

                nk_tree_pop(ctx);
            }

            /* COM GUI */
            bg.r = 0.10f, bg.g = 0.18f, bg.b = 0.24f, bg.a = 1.0f;

            // 1'000'000'000 ns => 1 second
            size_t timestamp_diff = (current_timestamp - last_timestamp);
            if (SerialPort.IsConnected() && (timestamp_diff > delay)) {
                /* Serial Stuff */
                last_timestamp = current_timestamp;
                read_count = SerialPort.ReadData(ptr, (mx_width - (2 * SIMDJSON_PADDING)));
                ptr[read_count] = 0;

                if (read_count > 0) { // -1 is failure so check > 0
                    size_t g = handle_json(parser, ctx, graphs, ptr, read_count);
                    graphs_to_display = (g > 0 && g != graphs_to_display) ? g : graphs_to_display;
                }
            } else if (demo_mode && example_json_mode) {
                /* Parsing Json Data */
                size_t g = handle_json(parser, ctx, graphs, mangled_example_json.data(), mangled_example_json.size());
                graphs_to_display = (g > 0 && g != graphs_to_display) ? g : graphs_to_display;
                last_ok_timestamp = current_timestamp;
                float flt_ts = current_timestamp / 1000000.0f;
                for (size_t i = 0; i < graphs_to_display; i++) {
                    for (size_t s = 0; s < graphs[i].values.size(); s++) {
                        graphs[i].values[s].back().x = flt_ts;
                    }
                }
            } else if (demo_mode) {
                /* Randomly Generated Data */
                last_ok_timestamp = current_timestamp;
                graphs_to_display = 6;
                float flt_ts = current_timestamp / 1000000.0f;
                for (size_t i = 0; i < graphs_to_display; i++) {
                    if (i >= graphs.size()) {
                        graphs.emplace_back();
                    }
                    // number of data points to show
                    graphs[i].limit = 60;
                    graphs[i].slots = 2;
                    if (graphs[i].title.empty()) {
                        graphs[i].title.clear();
                        fmt::format_to(std::back_inserter(graphs[i].title), "graph #{}", i);
                    }

                    // fill with all the potential colors
                    if (graphs[i].colors.size() < color_map.size()) {
                        size_t s = 0;
                        for (auto it = color_map.begin(); it != color_map.end(); it++) {
                            if (s >= NK_CHART_MAX_SLOT)
                                break;
                            graphs[i].colors.emplace_back(it->second);
                            s++;
                        }
                    }
                    graphs[i].values.reserve(graphs[i].colors.size());
                    graphs[i].labels.reserve(graphs[i].colors.size());
                    // make sure we have enough graphs for all the colors

                    while (graphs[i].values.size() < graphs[i].colors.size()) {
                        graphs[i].values.emplace_back();
                        graphs[i].labels.emplace_back();
                    }

                    // reserve to the limit
                    for (size_t s = 0; s < graphs[i].values.size(); s++) {
                        if (graphs[i].labels[s].empty()) {
                            graphs[i].labels[s].clear();
                            fmt::format_to(std::back_inserter(graphs[i].labels[s]), "data #{}", s);
                        }

                        if (graphs[i].values[s].capacity() < (graphs[i].limit + 1))
                            graphs[i].values[s].reserve(graphs[i].limit + 1);

                        for (size_t l = graphs[i].values[s].size(); l < graphs[i].limit; l++) {
#if __clang__
                            // graphs[i].values[s].emplace_back();
#else
                            if (graphs[i].values[s].size() < 1)
                                graphs[i].values[s].emplace_back(0.0f, 0.0f);
                            else
                                graphs[i].values[s].emplace_back(graphs[i].values[s].back().x + 0.001f,
                                                                 graphs[i].values[s].back().y +
                                                                     ((pcg32_random_r(&rng) % 256) / 1024.0f) -
                                                                     (128 / 1024.0f));
#endif
                        }
                    }

                    for (size_t s = 0; s < graphs[i].values.size(); s++) {
                        // fill with data point
                        auto it = graphs[i].values[s].end();
                        float rnd_value = ((pcg32_random_r(&rng) % 256) / 1024.0f) - (128 / 1024.0f);
                        struct nk_vec2 &point = graphs[i].values[s].unchecked_emplace_back();
                        point.y = it->y + rnd_value;
                        point.x = flt_ts;

                        if (graphs[i].values[s].size() > graphs[i].limit) {
                            graphs[i].values[s].erase(graphs[i].values[s].begin());
                        }
                        /* //if using std::vector
                        #if __clang__
                                                struct nk_vec2 &vec = graphs[i].values[s].emplace_back();
                                                if (graphs[i].values[s].size()) {
                                                    vec.x = flt_ts;
                                                    vec.y = graphs[i].values[s][graphs[i].values[s].size() - 1].y +
                                                            ((pcg32_random_r(&rng) % 256) / 1024.0f) - (128 / 1024.0f);
                                                }
                                                if (graphs[i].values[s].size() > graphs[i].limit) {
                                                    graphs[i].values[s].erase(graphs[i].values[s].begin());
                                                }
                        #else
                                                if (graphs[i].values[s].size() < 1)
                                                    graphs[i].values[s].emplace_back(flt_ts, 0.0f);
                                                graphs[i].values[s].emplace_back(flt_ts, graphs[i].values[s].back().y +
                                                                                             ((pcg32_random_r(&rng) %
                        256) / 1024.0f) - (128 / 1024.0f)); if (graphs[i].values[s].size() > graphs[i].limit) {
                                                    graphs[i].values[s].erase(graphs[i].values[s].begin());
                                                }
                        #endif
                        */
                    }
                }
            }

            struct nk_rect window_bounds_2 = nk_window_get_bounds(ctx);
            struct nk_rect content_region = nk_window_get_content_region(ctx);

            /* Dynamic render to fit graphs */
            nk_layout_row_dynamic(ctx, graph_height, (content_region.w / graph_width));

            for (size_t i = 0; i < graphs.size() && i < graphs_to_display; i++) {
                float min_value;
                float max_value;
                float min_ts;
                float max_ts;
                size_t offset = 0;

                if (graphs[i].values.size()) {
                    // figure out the ranges the data fills
                    min_ts = graphs[i].values[0][offset].x;
                    max_ts = graphs[i].values[0][offset].x;
                    min_value = graphs[i].values[0][offset].y;
                    max_value = graphs[i].values[0][offset].y;
                    for (size_t s = 0; s < graphs[i].values.size() && s < graphs[i].slots; s++) {
                        for (size_t idx = offset; idx < graphs[i].values[s].size(); idx++) {
                            min_ts = NK_MIN(graphs[i].values[s][idx].x, min_ts);
                            max_ts = NK_MAX(graphs[i].values[s][idx].x, max_ts);
                            min_value = NK_MIN(graphs[i].values[s][idx].y, min_value);
                            max_value = NK_MAX(graphs[i].values[s][idx].y, max_value);
                        }
                    }
                    // widen the view if somehow the data's perfectly flat
                    if (min_value == max_value) {
                        max_value = min_value + 1.0f;
                        graphs[i].upper_value.value = max_value;
                        graphs[i].lower_value.value = min_value;
                    }
                    if (min_ts == max_ts) {
                        max_ts = min_ts + 1.0f;
                    }

                    char hi_buffer[64];
                    auto num = std::to_chars(hi_buffer, hi_buffer + 64, max_value);
                    *num.ptr = 0;
                    char lo_buffer[64];
                    auto num2 = std::to_chars(lo_buffer, lo_buffer + 64, min_value);
                    *num2.ptr = 0;

                    {
                        struct nk_window *win;
                        struct nk_chart *chart;
                        const struct nk_style *config;
                        const struct nk_style_chart *style;

                        const struct nk_style_item *background;

                        // struct nk_rect widget_bounds = nk_widget_bounds(ctx);
                        struct nk_rect widget_bounds;
                        // reserve space for our graph
                        if (!ctx || !ctx->current || !ctx->current->layout) {
                            continue;
                        }
                        if (!nk_widget(&widget_bounds, ctx)) {
                            continue;
                        }

                        win = ctx->current;
                        config = &ctx->style;
                        chart = &win->layout->chart;
                        style = &config->chart;
                        background = &style->background;

                        struct nk_rect graph_bounds;
                        graph_bounds.x = widget_bounds.x + style->padding.x;
                        graph_bounds.y = widget_bounds.y + style->padding.y;
                        graph_bounds.w = widget_bounds.w - 2 * style->padding.x;
                        graph_bounds.h = widget_bounds.h - 2 * style->padding.y;
                        graph_bounds.w = NK_MAX(graph_bounds.w, 2 * style->padding.x);
                        graph_bounds.h = NK_MAX(graph_bounds.h, 2 * style->padding.y);

                        // draw our background
                        if (background->type == NK_STYLE_ITEM_IMAGE) {
                            nk_draw_image(&win->buffer, widget_bounds, &background->data.image, nk_white);
                        } else {
                            nk_fill_rect(&win->buffer, widget_bounds, style->rounding, style->border_color);
                            nk_fill_rect(&win->buffer, nk_shrink_rect(widget_bounds, style->border), style->rounding,
                                         style->background.data.color);
                        }

                        // draw our lines
                        size_t point_idx = 0;
                        // we clear here so reserve doesn't copy what should be an empty buffer
                        graphs[i].points.clear();
                        size_t coordinates = 0;
                        for (size_t s = 0; s < graphs[i].values.size(); s++)
                            coordinates += graphs[i].values[s].size();
                        if (coordinates > graphs[i].points.capacity())
                            graphs[i].points.reserve(coordinates * 4);
                        // graphs[i].points.reserve(graphs[i].limit * graphs[i].values.size());
                        float *data = graphs[i].points.data();

                        float xrange = max_ts - min_ts;
                        float yrange = max_value - min_value;
                        // make this an option
                        graphs[i].upper_value.lerp_v = zoom_rate;
                        graphs[i].lower_value.lerp_v = zoom_rate;
                        // make 0.05 an option
                        float yupper = graphs[i].upper_value.get_next_smooth_upper(max_value + (zoom_factor * yrange));
                        float ylower = graphs[i].lower_value.get_next_smooth_lower(min_value - (zoom_factor * yrange));

                        float x_range = max_ts - min_ts;
                        float y_range = yupper - ylower;
                        // float y_range = max_value - min_value;

                        float ylimrange = yupper - ylower;
                        float yspacing = ylimrange / (float)(yticks + 1);
                        // float yoffset = yspacing / 2.0f;
                        // float xstep = graph_bounds.w / graphs[i].limit;

                        for (size_t s = 0; s < graphs[i].values.size() && s < graphs[i].slots; s++) {
                            float *line_data = data + point_idx;
                            //

                            for (size_t idx = 0; idx < graphs[i].values[s].size(); idx++) {
                                line_data[idx * 2] =
                                    widget_bounds.x + (widget_bounds.w * ((graphs[i].values[s][idx].x - min_ts) /
                                                                          x_range)); // std::lerp(0.0f, x_range, );
                                line_data[(idx * 2) + 1] =
                                    (widget_bounds.y + widget_bounds.h) -
                                    (((graphs[i].values[s][idx].y - ylower) / ylimrange) * widget_bounds.h);

                                point_idx += 2;
                            }
                            nk_stroke_polyline_float(&ctx->current->buffer, line_data, graphs[i].values[s].size(),
                                                     line_width, graphs[i].colors[s]);

                            // struct nk_handle h;
                            // h.ptr = &graphs[i].lin
                            // nk_push_custom(&ctx->current->buffer, graph_bounds, render_polyline, );
                            struct nk_vec2 item_padding;
                            struct nk_text slot_text;
                            item_padding = (&ctx->style)->text.padding;

                            slot_text.padding.x = item_padding.x;
                            slot_text.padding.y = item_padding.y;
                            slot_text.background = (&ctx->style)->window.background;
                            slot_text.text = graphs[i].colors[s];
                            // chart.slots[slot].color;
                            // slot title
                            struct nk_rect slot_bounds;
                            slot_bounds = graph_bounds;

                            slot_bounds.y += (((&ctx->style)->font->height + 2.0f) * (s + 2));
                            slot_bounds.h -= 2 * (((&ctx->style)->font->height + 2.0f) * (s + 2));

                            slot_bounds.x += 2 * (graph_bounds.w / graphs[i].limit);
                            slot_bounds.w -= 4 * (graph_bounds.w / graphs[i].limit);

                            nk_widget_text(&ctx->current->buffer, slot_bounds, graphs[i].labels[s].c_str(),
                                           graphs[i].labels[s].size(), &slot_text, NK_TEXT_ALIGN_RIGHT,
                                           (&ctx->style)->font);
                        }
                        // use these uv functions b/c otherwise the coordinates can do a little dance
                        // draw ticks along vertical axis
                        float ydiv = 1.0f / (float)(yticks + 1);
                        for (size_t t = 0; t < yticks; t++) {
                            nk_chart_draw_line_uv(ctx, graph_bounds, (ydiv * (t + 1)), 10.0f, 2.0f,
                                                  nk_color{255, 255, 255, 255}, NK_TEXT_ALIGN_LEFT);
                            nk_chart_draw_value_uv(ctx, graph_bounds, ylower, yupper, (ydiv * (t + 1)), 10.0f, 2.0f,
                                                   nk_color{255, 255, 255, 255}, NK_TEXT_ALIGN_LEFT,
                                                   NK_TEXT_ALIGN_MIDDLE | NK_TEXT_ALIGN_LEFT);
                        }

                        // draw ticks along horizontal axis
                        float xdiv = 1.0f / (float)(xticks + 1);
                        for (size_t t = 0; t < xticks; t++) {
                            nk_chart_draw_line_uv(ctx, graph_bounds, xdiv * (t + 1), 10.0f, 2.0f,
                                                  nk_color{255, 255, 255, 255}, NK_TEXT_ALIGN_BOTTOM);

                            nk_chart_draw_value_uv(ctx, graph_bounds, min_ts, max_ts, xdiv * (t + 1), 10.0f, 2.0f,
                                                   nk_color{255, 255, 255, 255}, NK_TEXT_ALIGN_BOTTOM,
                                                   NK_TEXT_ALIGN_BOTTOM | NK_TEXT_ALIGN_CENTERED);
                        }

                        // Draw the title top centered
                        struct nk_text text_opts;
                        struct nk_vec2 item_padding;
                        item_padding = (&ctx->style)->text.padding;
                        // text settings
                        text_opts.padding.x = item_padding.x;
                        text_opts.padding.y = item_padding.y;
                        text_opts.background = (&ctx->style)->window.background;
                        text_opts.text = nk_color{255, 255, 255, 255}; // ctx->style.text.color;

                        nk_widget_text(&ctx->current->buffer, graph_bounds, graphs[i].title.data(),
                                       graphs[i].title.size(), &text_opts, NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_TOP,
                                       ctx->style.font);

                        // handle some user interfacing
                        // nk_flags ret;
                        // size_t hover_point;
                        if (!(ctx->current->layout->flags & NK_WINDOW_ROM)) {
                            // check if we're in bounds of a point
                            /*
                            for (size_t p = 0; p < point_idx; p += 2) {
                                struct nk_rect point_of_interest;
                                point_of_interest.x = data[p] - 2;
                                point_of_interest.y = data[p + 1] - 2;
                                point_of_interest.w = 6;
                                point_of_interest.h = 6;

                                ret = nk_input_is_mouse_hovering_rect(&ctx->input, point_of_interest);
                                if (ret) {
                                    ret = NK_CHART_HOVERING;
                                    ret |= ((&ctx->input)->mouse.buttons[NK_BUTTON_LEFT].down &&
                                            (&ctx->input)->mouse.buttons[NK_BUTTON_LEFT].clicked)
                                               ? NK_CHART_CLICKED
                                               : 0;
                                } else {
                                    continue;
                                }

                                if (ret & NK_CHART_HOVERING) {
                                    // do something when hoving over a point (show its x, y coordinate)
                                    char text[64];
                                    auto xchrs = std::to_chars(text, text + 64, data[p]);
                                    *xchrs.ptr = ',';
                                    auto chrs = std::to_chars(xchrs.ptr + 1, text + 64, data[p + 1]);
                                    size_t text_len = chrs.ptr - text;

                                    const struct nk_style *style = &ctx->style;
                                    struct nk_vec2 padding = style->window.padding;

                                    float text_width =
                                        style->font->width(style->font->userdata, style->font->height, text, text_len);
                                    text_width += (4 * padding.x);

                                    float text_height = (style->font->height + 2 * padding.y);

                                    if (nk_tooltip_begin(ctx, (float)text_width)) {
                                        nk_layout_row_dynamic(ctx, (float)text_height, 1);
                                        nk_text(ctx, text, text_len, NK_TEXT_LEFT);
                                        nk_tooltip_end(ctx);
                                    }
                                }
                            }
                            */
                            if (nk_input_is_mouse_hovering_rect(&ctx->input, graph_bounds) &&
                                (&ctx->input)->mouse.buttons[NK_BUTTON_LEFT].down) {

                                char text[64];
                                float xval = std::lerp(
                                    min_ts, max_ts, (((&ctx->input)->mouse.pos.x - graph_bounds.x) / graph_bounds.w));
                                auto xchrs = std::to_chars(text, text + 64, xval);
                                *xchrs.ptr = ',';

                                float yval = std::lerp(
                                    yupper, ylower, (((&ctx->input)->mouse.pos.y - graph_bounds.y) / graph_bounds.h));
                                auto chrs = std::to_chars(xchrs.ptr + 1, text + 64, yval);
                                size_t text_len = chrs.ptr - text;

                                const struct nk_style *style = &ctx->style;
                                struct nk_vec2 padding = style->window.padding;

                                float text_width =
                                    style->font->width(style->font->userdata, style->font->height, text, text_len);
                                text_width += (4 * padding.x);

                                float text_height = (style->font->height + 2 * padding.y);

                                if (nk_tooltip_begin(ctx, (float)text_width)) {
                                    nk_layout_row_dynamic(ctx, (float)text_height, 1);
                                    nk_text(ctx, text, text_len, NK_TEXT_LEFT);
                                    nk_tooltip_end(ctx);
                                }
                            }
                            if (nk_input_is_mouse_hovering_rect(&ctx->input, graph_bounds) &&
                                (&ctx->input)->keyboard.keys[NK_KEY_COPY].down &&
                                (&ctx->input)->keyboard.keys[NK_KEY_COPY].clicked) {
                                /*
                                cout_buffer = graphs_to_string(graphs);
                                std::cout << cout_buffer;
                                glfwSetClipboardString(glfw.win, cout_buffer.c_str());
                                cout_buffer.clear();
                                */
                            }
                        }
                    }
                }
            }
        }
        nk_end(ctx);

        /* Draw */
        glfwGetWindowSize(win, &width, &height);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(bg.r, bg.g, bg.b, bg.a);
        /* IMPORTANT: `nk_glfw_render` modifies some global OpenGL state
         * with blending, scissor, face culling, depth test and viewport and
         * defaults everything back into a default state.
         * Make sure to either a.) save and restore or b.) reset your own
         * state after rendering the UI. */
        nk_glfw3_render((nk_anti_aliasing)antialiasing); // NK_ANTI_ALIASING_ON
        glfwSwapBuffers(win);
        // Sleep(fps_delay);
        // Sleep(16);
        // Sleep(24);
        // Sleep(100);
    }

    if (SerialPort.IsConnected())
        fmt::print("{}", "disconnecting...");

    nk_glfw3_shutdown();
    glfwTerminate();

    return 0;
}