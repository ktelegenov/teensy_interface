#ifndef TEENSY_INTERFACE__YAML_HPP_
#define TEENSY_INTERFACE__YAML_HPP_

#include <yaml-cpp/yaml.h>
#include <boost/mpl/range_c.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/fusion/adapted/struct/detail/extension.hpp>
#include <boost/fusion/include/at.hpp>
#include <boost/fusion/include/value_at.hpp>
#include <boost/fusion/support/is_sequence.hpp>

#include <eigen3/Eigen/Core>

#include <optional>
#include <variant>
#include <string>
#include <tuple>
#include <utility>

#include "utils.hpp"

namespace atl
{

/* Recursive YAML reader for boost fusion containers */
template<typename Seq>
struct YAMLReader
{
  explicit YAMLReader(const YAML::Node & yaml, Seq & seq)
  : yaml_(yaml), seq_(seq) {}

  template<typename Index>
  void operator()(Index)
  {
    using TypeAtIndex = typename boost::fusion::result_of::value_at<Seq, Index>::type;
    const std::string name(boost::fusion::extension::struct_member_name<Seq, Index::value>::call());
    if (!yaml_[name] || yaml_[name].IsNull()) {
      if constexpr (atl::is_std_optional_v<TypeAtIndex>) {  //NOLINT
        boost::fusion::at<Index>(seq_) = std::nullopt;
        return;
      } else {
        throw YAML::KeyNotFound(YAML::Mark::null_mark(), name);
      }
    }

    try {
      boost::fusion::at<Index>(seq_) = yaml_[name].as<TypeAtIndex>();
    } catch (const YAML::RepresentationException & e) {
      throw YAML::KeyNotFound(YAML::Mark::null_mark(), name);
    }
  }

private:
  const YAML::Node & yaml_;
  Seq & seq_;
};


/* Recursive YAML writer for boost fusion containers */
template<typename Seq>
struct YAMLWriter
{
  explicit YAMLWriter(YAML::Node & yaml, const Seq & seq)
  : yaml_(yaml), seq_(seq) {}

  template<typename Index>
  void operator()(Index)
  {
    const std::string name(boost::fusion::extension::struct_member_name<Seq, Index::value>::call());
    yaml_[name] = boost::fusion::at<Index>(seq_);
  }

private:
  YAML::Node & yaml_;
  const Seq & seq_;
};

}  // namespace atl

namespace YAML
{

/* Overload yaml-cpp encode and decode functions for boost fusion containers */
template<typename T>
struct convert
{
  static Node encode(const T & val)
  {
    if constexpr (boost::fusion::traits::is_sequence<T>::value) {
      using Indices = boost::mpl::range_c<int, 0, boost::fusion::result_of::size<T>::value>;
      Node yaml;
      boost::mpl::for_each<Indices>(atl::YAMLWriter(yaml, val));
      return yaml;
    } else if constexpr (atl::is_scoped_enum_v<T>) {  //NOLINT
      using UT = std::underlying_type_t<T>;
      return Node(static_cast<UT>(val));
    } else if constexpr (atl::is_std_tuple_v<T>) {  //NOLINT
      Node yaml(NodeType::Sequence);
      atl::static_for_index<std::tuple_size_v<T>>(
        [&](auto i) {
          yaml.push_back(std::get<i.value>(val));
        });
      return yaml;

    } else if constexpr (std::is_base_of_v<Eigen::MatrixBase<T>, T>) {  //NOLINT
      Node yaml(NodeType::Sequence);
      yaml.SetStyle(EmitterStyle::value::Flow);
      for (Eigen::Index i = 0; i < val.size(); i++) {
        yaml.push_back(*(val.data() + i));
      }
      return yaml;

    } else if constexpr (atl::is_std_optional_v<T>) {  //NOLINT
      if (val.has_value()) {
        return Node(val.value());
      }
      return Node();

    } else {
      static_assert(
        ::atl::false_v<T>,
        "Unsupported type for YAML decoding.");
    }
  }

  static bool decode(const Node & yaml, T & val)
  {
    if constexpr (boost::fusion::traits::is_sequence<T>::value) {
      using Indices = boost::mpl::range_c<int, 0, boost::fusion::result_of::size<T>::value>;
      try {
        boost::mpl::for_each<Indices>(atl::YAMLReader(yaml, val));
      } catch (const std::exception & e) {
        return false;
      }
      return true;
    } else if constexpr (atl::is_scoped_enum_v<T>) {  //NOLINT
      try {
        using UT = std::underlying_type_t<T>;
        const auto v = yaml.as<UT>();
        if (v < static_cast<UT>(T::N_ELEMENTS)) {
          val = static_cast<T>(v);
        } else {
          val = T::UNKOWN;
        }
      } catch (const std::exception & e) {
        return false;
      }
      return true;
    } else if constexpr (atl::is_std_tuple_v<T>) {  //NOLINT
      if (!yaml.IsSequence()) {
        return false;
      }
      if (yaml.size() != std::tuple_size_v<T>) {
        return false;
      }

      atl::static_for_index<std::tuple_size_v<T>>(
        [&](auto i) {
          std::get<i.value>(val) = yaml[i].template as<std::tuple_element_t<i.value, T>>();
        });

      return true;
    } else if constexpr (std::is_base_of_v<Eigen::MatrixBase<T>, T>) {  //NOLINT
      if (!yaml.IsSequence()) {
        return false;
      }

      if constexpr (T::SizeAtCompileTime == Eigen::Dynamic) {
        if constexpr (T::IsVectorAtCompileTime) {
          val.resize(yaml.size());
        } else {
          return false;
        }
      } else if (T::SizeAtCompileTime != yaml.size()) {
        return false;
      }

      try {
        for (Eigen::Index i = 0; i < val.size(); i++) {
          *(val.data() + i) = yaml[i].template as<typename T::Scalar>();
        }
      } catch (const std::exception & e) {
        return false;
      }

      return true;
    } else if constexpr (atl::is_std_optional_v<T>) {  //NOLINT
      if (!yaml || yaml.IsNull()) {
        val = std::nullopt;
        return true;
      }
      try {
        using UT = typename T::value_type;
        val = yaml.as<UT>();
      } catch (const std::exception & e) {
        return false;
      }
      return true;
    } else {
      static_assert(
        ::atl::false_v<T>,
        "Unsupported type for YAML decoding.");
    }
  }
};

template<>
struct as_if<void, void>
{
  explicit as_if(const Node & node_)
  : node(node_) {}
  const Node & node;

  std::variant<void *, bool, int, double, std::string> operator()() const
  {
    if (node.IsNull()) {
      return {};
    }

    std::variant<void *, bool, int, double, std::string> out;

    bool val_bool;
    if (convert<bool>::decode(node, val_bool)) {
      out.emplace<bool>(val_bool);
      return out;
    }

    int val_int;
    if (convert<int>::decode(node, val_int)) {
      out.emplace<int>(val_int);
      return out;
    }

    double val_double;
    if (convert<double>::decode(node, val_double)) {
      out.emplace<double>(val_double);
      return out;
    }

    std::string val_string;
    if (convert<std::string>::decode(node, val_string)) {
      out.emplace<std::string>(std::move(val_string));
      return out;
    }

    return out;
  }
};

template<typename T>
struct as_if<T, std::optional<T>>
{
  explicit as_if(const Node & node_)
  : node(node_) {}
  const Node & node;

  std::optional<T> operator()() const
  {
    std::optional<T> val;
    T t;
    if (!node.IsNull() && convert<T>::decode(node, t)) {
      val = std::move(t);
    }

    return val;
  }
};

// There is already a std::string partial specialisation, so we need a full specialisation here
template<>
struct as_if<std::string, std::optional<std::string>>
{
  explicit as_if(const Node & node_)
  : node(node_) {}
  const Node & node;

  std::optional<std::string> operator()() const
  {
    std::optional<std::string> val;
    std::string t;
    if (!node.IsNull() && convert<std::string>::decode(node, t)) {
      val = std::move(t);
    }

    return val;
  }
};

}  // namespace YAML

#endif  // TEENSY_INTERFACE__YAML_HPP_