// ArduinoSerialPlotter.cpp : Defines the entry point for the application.
//
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include "ArduinoSerialPlotter.h"

#include "SerialClass.h" // Library described above
#include <format>
#include <stdio.h>
#include <string>
#include <tchar.h>
#include <unordered_map>
#include <math.h>

#include "simdjson.h"
using namespace simdjson;

#include <GL/glew.h>
#include <GLFW/glfw3.h>
//chart plotter api allows up to 6 colors, we'll do 8
#define NK_CHART_MAX_SLOT 8
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

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

std::unordered_map<std::string_view, nk_color> color_map = {
    {"red", nk_color(255, 0, 0, 255)},      {"green", nk_color(0, 255, 0, 255)},
    {"blue", nk_color(0, 0, 255, 255)},     {"orange", nk_color(255, 153, 51, 255)},
    {"yellow", nk_color(255, 255, 0, 255)}, {"pink", nk_color(255, 51, 204, 255)},
    {"purple", nk_color(172, 0, 230, 255)}, {"cyan", nk_color(0, 255, 255, 255)},
    {"white", nk_color(255, 255, 255, 255)}};

nk_color get_color(std::string_view v) {
    auto it = color_map.find(v);
    if (it != color_map.end()) {
        return it->second;
    } else {
        //return a consistant color for the word by using the hash
        size_t h = color_map.hash_function()(v);
        size_t idx = h % color_map.size();
        return std::next(color_map.begin(), idx)->second;
    }
}

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
0123456789 SIMDJSONPADDING
)raw";

static void error_callback(int e, const char *d) { printf("Error %d: %s\n", e, d); }

std::string cout_buffer;
template <typename... Args> void print_out(Args &&...args) {
    std::format_to(std::back_inserter(cout_buffer), std::forward<Args>(args)...);
    std::cout << cout_buffer;
    cout_buffer.clear();
}

struct graph_t {
    std::vector<std::vector<float>> values;
    std::vector<std::string> labels;
    std::vector<nk_color> colors;
    size_t limit = 60;
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

NK_API void nk_plot_multi(struct nk_context *ctx, enum nk_chart_type type, const float **values, int count, int offset,
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

NK_INTERN nk_flags nk_chart_draw_yticks(struct nk_context *ctx, struct nk_window *win, struct nk_chart *g, float yoffset, float xoffset, float spacing, nk_flags alignment) {
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
    //ratio = (value - g->slots[slot].min) / range;

    float half_step = step / 2.0f;

    float ytick_step = step * 0.25f;

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
    //cur.x = g->x + (float)(step * (float)g->slots[slot].index);
    cur.x = g->x;
    //skip a half step
    cur.y = yoffset;

    float bottom = (g->y + g->h);
    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float style_offset = bottom - (style->font->height / 2.0f);

        bounds.x = g->x + step + xoffset;
        bounds.h = style->font->height;
        
            //half_step + xoffset;
        //bounds.w = 20.0f;
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
            //get the width so we can print w/ a solid background
            bounds.w =
                style->font->width(style->font->userdata, style->font->height, (const char *)floating_point, len);

            // write text
            nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
            nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
                           style->font);
        }   
    } else { //NK_TEXT_ALIGN_RIGHT
        float style_offset = bottom - (style->font->height / 2.0f);

        float right = (g->x + g->w);
        bounds.h = style->font->height;
        bounds.w = half_step + xoffset;
        bounds.x = ((right - step) - bounds.w) - xoffset;

        for (; cur.y < h; cur.y += spacing) {
            nk_stroke_line(out, right-half_step, bottom - cur.y, right, bottom - cur.y, 2.0f,
                           nk_color{255, 255, 255, 255});

            float percentage = cur.y / h;
            float value = std::lerp(g->slots[0].min, g->slots[0].max, percentage);
            char floating_point[64];
            auto label = std::to_chars(floating_point, (floating_point) + 64, value);
            *label.ptr = 0;
            size_t len = label.ptr - floating_point;
            bounds.y = style_offset - cur.y;

            // get the width so we can print w/ a solid background
            bounds.w =
                style->font->width(style->font->userdata, style->font->height, (const char *)floating_point, len);
            // write text
            nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
            nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
                           style->font);
        } 
    }

    return ret;
}
NK_INTERN nk_flags nk_chart_draw_yticks(struct nk_context *ctx, int ticks, nk_flags alignment) {
    float h = ctx->current->layout->chart.h;
    float spacing = h / (ticks + 1);
    float yoffset = spacing / 2.0f;
    return nk_chart_draw_yticks(ctx, ctx->current, &ctx->current->layout->chart, yoffset, 0.0f, spacing, alignment);

}
NK_INTERN nk_flags nk_chart_draw_ytick_value(struct nk_context *ctx, struct nk_window *win, struct nk_chart *g,
                                             float xoffset, float value,
                                             nk_flags alignment) {
    struct nk_panel *layout = win->layout;
    const struct nk_input *i = &ctx->input;
    struct nk_command_buffer *out = &win->buffer;

    const float yoffset = 0.0f;

    nk_flags ret = 0;
    struct nk_vec2 cur;
    struct nk_rect bounds;
    struct nk_color color;
    float step;
    float range;
    float ratio;

    step = g->w / (float)g->slots[0].count;
    range = g->slots[0].max - g->slots[0].min;
    ratio = (value - g->slots[0].min) / range;

    float half_step = step / 2.0f;

    float ytick_step = step * 0.25f;

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

    float bottom = (g->y + g->h);
    if (alignment & NK_TEXT_ALIGN_LEFT) {
        float style_offset = bottom - (style->font->height / 2.0f);

        bounds.x = g->x + step + xoffset;
        bounds.h = style->font->height;
        //bounds.w = half_step + xoffset;
        bounds.y = style_offset - (ratio * g->h);

        float y = bottom - (ratio * g->h);

        nk_stroke_line(out, g->x, y, g->x + half_step, y, 2.0f, nk_color{255, 255, 255, 255});

        char floating_point[64];
        auto label = std::to_chars(floating_point, (floating_point) + 64, value);
        *label.ptr = 0;
        size_t len = label.ptr - floating_point;

        //get the width of the text
        bounds.w = style->font->width(style->font->userdata, style->font->height, (const char *)floating_point, len);

        // write text
        nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
        nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
            style->font);

    } else { // NK_TEXT_ALIGN_RIGHT
        float style_offset = bottom - (style->font->height / 2.0f);

        float right = (g->x + g->w);
        bounds.h = style->font->height;
        //bounds.w = half_step + xoffset;
        bounds.x = ((right - step) - bounds.w) - xoffset;
        bounds.y = style_offset - (ratio * g->h);

        float y = bottom - (ratio * g->h);

        nk_stroke_line(out, right - half_step, y, right, y, 2.0f, nk_color{255, 255, 255, 255});

        char floating_point[64];
        auto label = std::to_chars(floating_point, (floating_point) + 64, value);
        *label.ptr = 0;
        size_t len = label.ptr - floating_point;
        // get the width of the text
        bounds.w = style->font->width(style->font->userdata, style->font->height, (const char *)floating_point, len);

        // write text
        nk_fill_rect(&win->buffer, bounds, 0.0f, style->chart.background.data.color);
        nk_widget_text(&win->buffer, bounds, floating_point, len, &text, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM,
                       style->font);
    }

    return ret;
}
NK_INTERN nk_flags nk_chart_draw_ytick_value(struct nk_context *ctx, float value, nk_flags alignment) {
    return nk_chart_draw_ytick_value(ctx, ctx->current, &ctx->current->layout->chart, 0.0f, value, alignment);
}

NK_INTERN nk_flags nk_chart_title(struct nk_context *ctx, const char* title, size_t len, nk_flags alignment) {
    struct nk_rect bounds;
    const struct nk_style *style = &ctx->style;

    struct nk_vec2 item_padding;
    struct nk_text text;

    item_padding = style->text.padding;
    //text settings
    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = style->window.background;
    text.text = ctx->style.text.color;

    nk_chart &chart = ctx->current->layout->chart;
    bounds.x = chart.x;
    bounds.y = chart.y;
    bounds.w = chart.w;
    bounds.h = style->font->height;
    // write text
    //nk_fill_rect(&ctx->current->buffer, bounds, 0.0f, style->chart.background.data.color);
    nk_widget_text(&ctx->current->buffer, bounds, title, len, &text, alignment, style->font);
    return nk_flags{0};
}

NK_INTERN nk_flags nk_chart_slot_title(struct nk_context *ctx, const char *title, size_t len, nk_flags alignment, int slot) {
    struct nk_rect bounds;
    const struct nk_style *style = &ctx->style;

    struct nk_vec2 item_padding;
    struct nk_text text;

    nk_chart &chart = ctx->current->layout->chart;

    item_padding = style->text.padding;
    // text settings
    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = style->window.background;
    text.text = chart.slots[slot].color;

    float step = chart.w / (float)chart.slots[0].count;

    bounds.x = chart.x + 2*step;
    bounds.y = chart.y + ((style->font->height+2.0f) * slot);
    bounds.w = chart.w - 4*step;
    bounds.h = style->font->height;
        
    // write text
    // nk_fill_rect(&ctx->current->buffer, bounds, 0.0f, style->chart.background.data.color);
    nk_widget_text(&ctx->current->buffer, bounds, title, len, &text, alignment, style->font);
    return nk_flags{0};
}

bool handle_json(struct nk_context *ctx, std::vector<graph_t> &graphs, const char *ptr, uint32_t read_count,
                 uint32_t alloc_width) {
    if (read_count) {
        std::string_view v{ptr, (size_t)read_count};
        // std::cout << v << '\n';
        ondemand::parser parser;
        ondemand::document graph_data;
        // padded_string json(ptr, (size_t)read_count);
        bool good_utf8 = simdjson::validate_utf8(ptr, read_count);
        auto semi_colon = std::find(ptr, ptr + read_count, '{');

        size_t dist = semi_colon - ptr;
        padded_string_view json(semi_colon, read_count - dist, (alloc_width - 1) - dist);

        auto error = parser.iterate(json).get(graph_data);
        if (good_utf8 && !error) {
            bool result = true;
            try {
                //get the view of the json object
                std::string_view v;
                auto error = simdjson::to_json_string(graph_data).get(v);
                if (error) {
                    return false;
                }
                
                size_t ts;
                auto err = graph_data["t"].get(ts);
                if (err) {
                    // could not find timestamp field
                    std::string_view v{ptr, read_count};
                    std::cout << v;
                    return false;
                } else {
                    ondemand::array graphs_array;
                    auto graphs_err = graph_data["g"].get_array().get(graphs_array);
                    size_t g = 0;
                    if (graphs_err) {
                        // could not find the g field (graphs)
                        return false;
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
                            //if we have a color for the index it overrides the hashed one
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

                            ondemand::array data_points = graph["d"];
                            float mn = std::numeric_limits<float>::max();
                            float mx = std::numeric_limits<float>::min();
                            uint32_t count = 0;
                            for (auto value : data_points) {
                                if (count >= graphs[g].values.size()) {
                                    graphs[g].values.emplace_back();
                                    graphs[g].values[count].reserve(graphs[g].limit);
                                    graphs[g].labels.emplace_back("");
                                    graphs[g].colors.emplace_back(ctx->style.chart.color);
                                }
                                graphs[g].values[count].emplace_back((double)value);
                                //
                                if (graphs[g].values[count].size() > graphs[g].limit) {
                                    graphs[g].values[count].erase(graphs[g].values[count].begin());
                                }

                                count++;
                            }
                            g++;
                        }
                    }
                }
            } catch (const std::exception &exc) {
                std::cout << exc.what() << '\n';
                std::cout << v << '\n';
                result = false;
            }
            return result;
        } else {
            return false;
        }
        return true;
    } else {
        return false;
    }
}

// application reads from the specified serial port and reports the collected
// data
int main(int argc, char *argv[]) {
    std::vector<graph_t> graphs;
    graphs.reserve(32);

    cout_buffer.reserve(1024);
    /*GUI THINGS*/
    /* Platform */
    static GLFWwindow *win;
    int width = 0, height = 0;
    struct nk_context *ctx;
    struct nk_colorf bg;
    struct nk_image img;

    uint32_t window_width = 1920;
    uint32_t window_height = 1080;
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
    win = glfwCreateWindow(window_width, window_height, "Demo", NULL, NULL);
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
        struct nk_font *clean = nk_font_atlas_add_from_file(atlas,
         "../../../extra_font/ProggyClean.ttf", 12, 0);
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

    size_t mx_width = 1024;
    size_t alloc_width = 1024 + SIMDJSON_PADDING;
    char *ptr = (char *)calloc(alloc_width, 1);
    size_t sz = _msize(ptr);
    sz = sz > alloc_width ? sz : alloc_width;

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
            txtedit_len[0] = result.ptr - txtedit;
            break;
        }
    }

    int read_count = 0;
    // make sure we 0
    ptr[0] = 0;
    char *bufend = ptr;
    size_t i = 0;

    std::string comport_path;
    std::string graph_title;

    size_t last_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    bool demo_mode = true;
    bool example_json_mode = false;

    nk_colorf picker_color;
    /* Main Loop */
    while (!glfwWindowShouldClose(win)) {
        /* Input */
        glfwPollEvents();
        nk_glfw3_new_frame();

        if (nk_begin(ctx, "Serial Plotter", nk_rect(0, 0, width, height),
                     NK_WINDOW_BORDER | NK_WINDOW_SCALABLE)) 
            //NK_WINDOW_SCROLL_AUTO_HIDE
            //NK_WINDOW_NO_SCROLLBAR
            // NK_WINDOW_MOVABLE
                                                                 // NK_WINDOW_CLOSABLE
        {
            nk_menubar_begin(ctx);
            {
                nk_layout_row_dynamic(ctx, 30, 3);

                // nk_layout_row_dynamic(ctx, 120, 2);
                nk_label(ctx, "COM Port:", NK_TEXT_LEFT);
                nk_edit_string(ctx, NK_EDIT_SIMPLE, txtedit, txtedit_len, 4, nk_filter_decimal);

                if (SerialPort.IsConnected()) {
                    demo_mode = false;
                    if (nk_button_label(ctx, "Disconnect")) {
                        SerialPort.Disconnect();
                    }
                } else {
                    if (nk_button_label(ctx, "Connect")) {
                        comport_path.clear();
                        std::format_to(std::back_inserter(comport_path), std::string_view{"\\\\.\\COM{}"},
                                       std::string_view{txtedit, (size_t)txtedit_len[0]});

                        print_out(std::string_view{"attempting to connect to {}...\n"}, comport_path);

                        uint32_t port_num = 0;
                        std::from_chars(txtedit, (txtedit + ((size_t)txtedit_len)), port_num, 10);
                        int result = SerialPort.Connect(port_num, false, 115200);
                        if (result == 0)
                            result = SerialPort.Connect(comport_path.data(), false, 115200);
                        print_out("{}", result != 0 ? "success!" : "failed!");

                        if (result)
                            demo_mode = false;
                    }
                }
            }
            nk_menubar_end(ctx);

            /* COM GUI */
            bg.r = 0.10f, bg.g = 0.18f, bg.b = 0.24f, bg.a = 1.0f;
            size_t current_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            // 1'000'000'000
            if (SerialPort.IsConnected() &&
                ((current_timestamp - last_timestamp) > 300'000'000)) { // attempt to graph graphs from incoming
                                                                        // data
                /* Serial Stuff */
                last_timestamp = current_timestamp;
                read_count = SerialPort.ReadData(ptr, (mx_width - (2 * SIMDJSON_PADDING)));
                ptr[read_count] = 0;

                if (read_count) {
                    handle_json(ctx, graphs, ptr, read_count, alloc_width);
                }
            } else if (demo_mode && example_json_mode) {
                handle_json(ctx, graphs, example_json.data(), example_json.size() - (2*SIMDJSON_PADDING), example_json.size());
            } else if (demo_mode) {
                for (size_t i = 0; i < 5; i++) {
                    if (i >= graphs.size()) {
                        graphs.emplace_back();
                    }
                    // number of data points to show
                    graphs[i].limit = 60;
                    if (graphs[i].title.empty()) {
                        graphs[i].title.clear();
                        std::format_to(std::back_inserter(graphs[i].title), "graph #{}", i);
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
                            std::format_to(std::back_inserter(graphs[i].labels[s]), "data #{}", s);
                        }

                        graphs[i].values[s].reserve(graphs[i].limit + 1);
                        for (size_t l = graphs[i].values[s].size(); l < graphs[i].limit; l++) {
                            graphs[i].values[s].emplace_back();
                        }
                    }

                    for (size_t s = 0; s < graphs[i].values.size(); s++) {
                        // fill with data point
                        if (graphs[i].values[s].size() < 1)
                            graphs[i].values[s].emplace_back();
                        graphs[i].values[s].emplace_back(graphs[i].values[s].back() + ((rand() % 256) / 1024.0f) -
                                                         (128 / 1024.0f));
                        if (graphs[i].values[s].size() > graphs[i].limit) {
                            graphs[i].values[s].erase(graphs[i].values[s].begin());
                        }
                    }
                }
            }

            for (size_t i = 0; i < graphs.size(); i++) {
                float min_value;
                float max_value;
                size_t offset = 0;

                if (graphs[i].values.size()) {
                    min_value = graphs[i].values[0][offset];
                    max_value = graphs[i].values[0][offset];
                    for (size_t s = 0; s < graphs[i].values.size(); s++) {
                        for (size_t idx = offset; idx < graphs[i].values[s].size(); idx++) {
                            min_value = NK_MIN(graphs[i].values[s][idx], min_value);
                            max_value = NK_MAX(graphs[i].values[s][idx], max_value);
                        }
                    }
                    if (min_value == max_value)
                        max_value = min_value + 1.0f;

                    char hi_buffer[64];
                    auto num = std::to_chars(hi_buffer, hi_buffer + 64, max_value);
                    *num.ptr = 0;
                    char lo_buffer[64];
                    auto num2 = std::to_chars(lo_buffer, lo_buffer + 64, min_value);
                    *num2.ptr = 0;

                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, hi_buffer, NK_TEXT_ALIGN_LEFT);
                    nk_layout_row_dynamic(ctx, 240, 1);
                    if (nk_chart_begin_colored(ctx, nk_chart_type::NK_CHART_LINES, graphs[i].colors[0],
                                               ctx->style.chart.selected_color, graphs[i].limit, min_value,
                                               max_value)) {

                        for (size_t s = 0; s < graphs[i].values.size() && s < NK_CHART_MAX_SLOT; s++) {
                            // ctx->style.chart.color
                            if (s > 0)
                                nk_chart_add_slot_colored(ctx, nk_chart_type::NK_CHART_LINES, graphs[i].colors[s],
                                                          ctx->style.chart.selected_color, graphs[i].limit, min_value,
                                                          max_value);
                            for (size_t idx = offset; idx < graphs[i].values[s].size(); idx++) {
                                nk_chart_push_slot(ctx, graphs[i].values[s][idx], s);
                            }
                        }
                        //nk_chart_draw_yticks(ctx, ctx->current, &ctx->current->layout->chart, 14.0f, 0.0f, 28.0f, NK_TEXT_ALIGN_RIGHT);
                        nk_chart_draw_yticks(ctx, 8, NK_TEXT_ALIGN_LEFT);
                        nk_chart_draw_ytick_value(ctx, max_value, NK_TEXT_ALIGN_LEFT);
                        nk_chart_draw_ytick_value(ctx, min_value, NK_TEXT_ALIGN_LEFT);

                        for (size_t s = 0; s < graphs[i].values.size() && s < NK_CHART_MAX_SLOT; s++) {
                            //have a line that prints / tracks the last value
                            nk_chart_draw_ytick_value(ctx, graphs[i].values[s].back(), NK_TEXT_ALIGN_RIGHT);
                            nk_chart_slot_title(ctx, graphs[i].labels[s].c_str(), graphs[i].labels[s].size(),
                                                NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_MIDDLE, s);
                        }

                        nk_chart_title(ctx, graphs[i].title.c_str(), graphs[i].title.size(), NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE);
                        
                        nk_chart_end(ctx);
                    }

                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, lo_buffer, NK_TEXT_ALIGN_LEFT);
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
        nk_glfw3_render(NK_ANTI_ALIASING_ON);
        glfwSwapBuffers(win);
        // Sleep(100);
    }

    if (SerialPort.IsConnected())
        std::cout << "disconnecting...";

    nk_glfw3_shutdown();
    glfwTerminate();
    // delete SP;

    return 0;
}