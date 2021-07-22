#include "utility/argument_parser.hpp"

#include <cxxabi.h>
#include <format>
#include <functional>
#include <iostream>
#include <queue>
#include <spdlog/spdlog.h>
#include <utility>

namespace argparse {

argparse_error
argument_parser::parse_args(const std::vector<std::string> &params) {
  std::unordered_map<char, unsigned>        short_args{};
  std::unordered_map<std::string, unsigned> long_args{};
  std::queue<unsigned>                      positional_args{};

  argparse_error return_result{};

  spdlog::trace("Available arguments:");
  for (const auto &arg_pair : m_args) {
    spdlog::trace((std::string)arg_pair.second);
    if (arg_pair.second.is_positional()) {
      positional_args.push(arg_pair.second.id);
      continue;
    }
    if (!arg_pair.second.get_short_names().empty()) {
      for (const auto &short_name : arg_pair.second.get_short_names()) {
        short_args[short_name] = arg_pair.second.id;
      }
    }
    if (!arg_pair.second.get_long_names().empty()) {
      for (const auto &long_name : arg_pair.second.get_long_names()) {
        long_args[long_name] = arg_pair.second.id;
      }
    }
  }

  for (unsigned int i = 0; i < params.size(); i++) {
    const std::string          param = params[i];
    std::optional<std::string> value = {};
    if (i + 1 < params.size()) {
      value = params[i + 1];
    }

    //    std::cout << std::format("Examining param: {}", param) << std::endl;

    if (param[0] == '-' && param[1] == '-') {
      std::string param_name = param.substr(2);
      if (long_args.contains(param_name)) {
        argument &arg = m_args[long_args[param_name]];
        //        std::cout << std::format("Long argument: {}, arg: {} ({})",
        //                                 param,
        //                                 arg.get_long_names(),
        //                                 arg.get_short_names()) << std::endl;
        if (arg.is_flag()) {
          arg.set_value("true");
        } else if (value.has_value()) {
          arg.set_value(value.value());
        } else {
          return_result = std::format(
              "Expected argument for parameter '{}', none were found!",
              param_name);
        }
      } else {
        return_result = std::format("Unknown argument {}!", param_name);
      }

    } else if (param[0] == '-') {
      for (const char &short_param : param.substr(1)) {
        if (short_args.contains(short_param)) {
          auto &arg = m_args[short_args[short_param]];

          if (arg.is_flag()) {
            arg.set_value("true");
          } else if (value.has_value()) {
            arg.set_value(value.value());
          } else {
            return_result = std::format(
                "Expected argument for parameter '{}', none were found!",
                short_param);
          }
        } else {
          return_result = std::format("Unknown argument {}!", short_param);
        }
      }
    } else if (!positional_args.empty()) { // positional arg
      auto &arg = m_args[positional_args.front()];

      spdlog::trace("Setting arg '{}' to {}", arg.get_name(), param);

      arg.set_value(std::string(param));

      positional_args.pop();

    } else {
      return_result = std::format("Argument '{}' does not exist!", param);
    }
  }

  for (const auto &arg_pair : m_args) {
    spdlog::trace((std::string)arg_pair.second);
  }

  return return_result;
}

argument &argument_parser::add_argument() {
  argument arg{};
  m_args.emplace(arg.id, arg);
  return m_args[arg.id];
}

argument &argument_parser::add_argument(const argument &arg) {
  m_args.emplace(arg.id, arg);
  return m_args[arg.id];
}

argument &argument_parser::add_argument(std::string name) {
  argument arg{name};
  m_args.emplace(arg.id, arg);
  return m_args[arg.id];
}

argparse_error argument_parser::parse_args(const int argc, const char **argv) {
  std::vector<std::string> args(argc);
  for (unsigned i = 0; i < argc; i++) {
    args[i] = argv[i];
  }
  return parse_args(args);
}

argument_parser::argument_parser(std::string program_name)
    : m_program_name(std::move(program_name)) {
  add_argument("command").positional().type(typeid(std::string));
}

std::optional<argument> argument_parser::get_arg(const std::string &name) {
  for (const auto &arg : m_args) {
    if (arg.second == name) {
      return arg.second;
    }
  }
  return {};
}

argument_value argument_parser::operator[](const std::string &name) {
  const auto &arg = get_arg(name);

  if (!arg.has_value()) {
    throw std::runtime_error(std::format("Unknown argument: {}", name));
  }

  return argument_value(arg->get_value(), arg->get_type());
}

argument_value::operator bool() const {
  if (!m_value.has_value())
    return false;
  if (m_type != typeid(bool))
    return false;

  return m_value == "true";
}

std::string argument_parser::get_program_name() {
  if (m_program_name.empty())
    return get_arg("command")->get_value().value_or("???");

  return m_program_name;
}

void argument_parser::print_help() {
  std::string help_options_message;
  std::string help_positionals_message;
  for (const auto &arg_pair : m_args) {
    const auto &arg = arg_pair.second;
    if (arg.is_positional()) {
      if (arg.get_name() == "command")
        continue;

      std::format_to(std::back_inserter(help_positionals_message),
                     "\t{:32}\t\t{}\n", arg.get_name(),
                     arg.get_description().value_or(""));
    } else {
      std::vector<std::string> option_names;
      for (const auto &short_name : arg.get_short_names()) {
        option_names.push_back(std::string("-") + short_name);
      }
      for (const auto &long_name : arg.get_long_names()) {
        option_names.push_back(std::string("--").append(long_name));
      }
      std::string option_string;
      for (auto iterator = option_names.begin(); iterator != option_names.end();
           ++iterator) {
        option_string.append(*iterator);
        if (iterator + 1 != option_names.end()) {
          option_string.append(", ");
        }
      }
      std::format_to(std::back_inserter(help_options_message),
                     "\t{:32}\t\t{}\n", option_string,
                     arg.get_description().value_or(""));
    }
  }
  std::cout << std::format("Usage: {} [options...] positionals\n"
                           "Options:\n"
                           "{}\n"
                           "Positionals:\n"
                           "{}\n",
                           get_program_name(), help_options_message,
                           help_positionals_message)
            << std::flush;
}

} // namespace argparse