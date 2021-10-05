#pragma once
//https://gist.github.com/marchelbling/658f6c3f7c882974eb96
#include "utf8_json.h"
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <ostream>

template <typename T> struct is_container : std::false_type {};

template <typename T> struct is_associative_container : std::false_type {};

// vector overload
template <typename T> struct is_container<std::vector<T>> : std::true_type {};

// map overload
template <typename K, typename V> struct is_container<std::map<K, V>> : std::true_type {};

template <typename V> struct is_associative_container<std::map<std::string, V>> : std::true_type {};

class json_stream {

  public:
    json_stream(const std::string &path) : _stream(path.c_str()) {}

    template <typename T> json_stream &operator<<(const T &data) {
        if (_stream.is_open()) {
            _stream << dump(data);
        }
        return *this;
    }

  private:
    // forward standard manipulator like std::endl
    typedef std::ostream &(*ostream_manipulator)(std::ostream &);
    json_stream &operator<<(ostream_manipulator pf) {
        if (_stream.is_open()) {
            _stream << pf;
        }
        return *this;
    }

    template <typename T> std::string dump(const T &t) const {
        // dispatch to actual dump method:
        // * not iterable type dumped as simple value
        // * iterable type
        //   * with mapped value dumped as mapped_container
        //   * otherwise dumped as simple_container
        return dump_value_or_container(t, typename is_container<T>::type());
    }

    // dispatch to correct dump method
    template <typename T> std::string dump_value_or_container(const T &t, std::false_type) const {
        return dump_value(t);
    }

    template <typename T> std::string dump_value_or_container(const T &t, std::true_type) const {
        return dump_simple_or_associative_container(t, typename is_associative_container<T>::type());
    }

    template <typename T> std::string dump_simple_or_associative_container(const T &t, std::false_type) const {
        return dump_simple_container(t);
    }

    template <typename T> std::string dump_simple_or_associative_container(const T &t, std::true_type) const {
        return dump_associative_container(t);
    }

    // implement type specific serialization
    template <typename V> std::string dump_value(const V &value) const {
        std::ostringstream oss;
        oss << sanitize(value);
        return oss.str();
    }

    std::string dump_value(const std::string &value) const { return "\"" + sanitize(value) + "\""; }

    template <typename K, typename V> std::string dump_value(const std::pair<const K, V> &pair) const {
        std::ostringstream oss;
        oss << "[" << dump(pair.first) << ", " << dump(pair.second) << "]";
        return oss.str();
    }

    template <typename V> std::string dump_pair(const std::pair<const std::string, V> &pair) const {
        std::ostringstream oss;
        oss << dump(pair.first) << ": " << dump(pair.second);
        return oss.str();
    }

    template <typename C> std::string dump_simple_container(const C &container) const {
        std::ostringstream oss;
        typename C::const_iterator it = container.begin();

        oss << "[" << dump(*it);
        for (++it; it != container.end(); ++it) {
            oss << ", " << dump(*it);
        }
        oss << "]";

        return oss.str();
    }

    template <typename M> std::string dump_associative_container(const M &map) const {
        std::ostringstream oss;
        typename M::const_iterator it = map.begin();

        oss << "{" << dump_pair(*it);
        for (++it; it != map.end(); ++it) {
            oss << ", " << dump_pair(*it);
        }
        oss << "}";

        return oss.str();
    }

    template <typename T> T sanitize(const T &t) const { return t; }

    template <typename T> int sgn(const T &val) const { return (T(0) < val) - (val < T(0)); }

    double sanitize(const double d) const {
        if (std::isfinite(d)) {
            return d;
        } else {
            if (std::isinf(d)) {
                return sgn(d) * std::numeric_limits<double>::max();
            }
            return 0.;
        }
    }

    double sanitize(const float f) const { return sanitize(static_cast<double>(f)); }

    std::string sanitize(std::string const &input) const {
        return utf8_json::json_encode_codepoints(utf8_json::decode_utf8(input));
    }

    std::ofstream _stream;
};