// ArduinoSerialPlotter.cpp : Defines the entry point for the application.
//
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#define XXH_INLINE_ALL
#include "ArduinoSerialPlotter.h"
#include "real_vector.h"
#include "xxhash.h"

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

#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/imgui.h"

#include "imgui/implot.h"
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

using nk_color = ImColor;
using nk_flags = uint32_t;
using nk_vec2 = ImVec2;

std::unordered_map<std::string_view, ImU32> color_map = {
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
    real::vector<real::vector<ImVec2>> values;
    // x (evens), y (odds)
    real::vector<double> points;
    real::vector<std::string> labels;
    real::vector<ImU32> colors;

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

std::string stream_buffer;
// returns the number of graphs to draw, 0 -> no data / no change
size_t handle_json(ondemand::parser &parser, real::vector<graph_t> &graphs, const char *ptr, uint32_t read_count) {
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
                                            ImU32 white = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
                                            graphs[g].colors.emplace_back(white);
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
                                            ImU32 white = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
                                            graphs[g].colors.emplace_back(white);
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
                                            ImU32 white = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
                                            graphs[g].colors.emplace_back(white);
                                        }

                                        ImVec2 point;
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
    glfwMakeContextCurrent(win);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    ImPlot::CreateContext();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    const char *glsl_version = "#version 130";
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

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
    bool vsync = true;
    /* Turn on VSYNC */
    glfwSwapInterval(vsync);
    size_t previous_timestamp = 0;
    //int demo_mode = 1;
    bool demo_mode = true;
    int graphs_to_display = 0;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    smooth_data<double> smooth_fps;
    smooth_fps.lerp_v = 0.0001;
    smooth_fps.value = 60.0;
    while (!glfwWindowShouldClose(win)) {
        /* Input */
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        size_t vw_height = graphs.size() * 240 + 200;
        /* Do timestamp things */
        size_t current_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        size_t timestamp_diff = current_timestamp - previous_timestamp;
        previous_timestamp = current_timestamp;
        
        constexpr double ns_per_second = 1'000'000'000.0;
        double fps = 1.0 / ((double)timestamp_diff / (double)ns_per_second);
        double fps_smoothed = smooth_fps.get_next_smooth(fps);
        //ImGui::ShowDemoWindow();

        {
            ImGui::Begin("Hello World");
            
            ImGui::Checkbox("Demo", &demo_mode);
            //bool nvsync = vsync;
            ImGui::Checkbox("VSync", &vsync);
            glfwSwapInterval(vsync);
            //ImGui::LabelText("FPS")
            ImGui::LabelText("FPS", "%.2f", fps_smoothed);

            if (demo_mode) {
                /* Randomly Generated Data */
                previous_timestamp = current_timestamp;
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
                        struct ImVec2 &point = graphs[i].values[s].unchecked_emplace_back();
                        point.y = it->y + rnd_value;
                        point.x = flt_ts;

                        if (graphs[i].values[s].size() > graphs[i].limit) {
                            graphs[i].values[s].erase(graphs[i].values[s].begin());
                        }
                    }
                }
            }

            for (size_t i = 0; i < graphs.size() && i < graphs_to_display; i++) {
                double xmin = std::numeric_limits<double>::max();
                double xmax = std::numeric_limits<double>::min();
                double ymin = std::numeric_limits<double>::max();
                double ymax = std::numeric_limits<double>::min();
                size_t slot_count = std::min(graphs[i].values.size(), graphs[i].slots);
                for (size_t s = 0; s < slot_count; s++) {
                    for (size_t idx = 0; idx < graphs[i].values[s].size(); idx++) {
                        xmin = std::min(xmin, (double)graphs[i].values[s][idx].x);
                        xmax = std::max(xmax, (double)graphs[i].values[s][idx].x);
                        ymin = std::min(ymin, (double)graphs[i].values[s][idx].y);
                        ymax = std::max(ymax, (double)graphs[i].values[s][idx].y);
                    }
                }
                //make sure the gui tracks the points
                
                ImPlot::SetNextPlotLimits(xmin,xmax,ymin,ymax,ImGuiCond_Always);
                if (ImPlot::BeginPlot(graphs[i].title.c_str(), "Time")) {
                    for (size_t s = 0; s < slot_count; s++) {
                        //ImPlot::PlotLine(graphs[i].labels[s].c_str(),)
                        ImPlot::PlotLineG(graphs[i].labels[s].c_str(), [](void* data, int idx){
                            float *ptr = (float*)data;
                            return ImPlotPoint(ptr[idx*2], ptr[idx*2+1]);
                        }, graphs[i].values[s].data(),graphs[i].values[s].size());
                    }
                    ImPlot::EndPlot();
                }
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(win, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                     clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(win);
        // Sleep(fps_delay);
        // Sleep(16);
        // Sleep(24);
        // Sleep(100);
    }

    if (SerialPort.IsConnected())
        fmt::print("{}", "disconnecting...");

    glfwTerminate();

    return 0;
}