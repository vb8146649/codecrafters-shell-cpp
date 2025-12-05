#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#define ALL(s) (s).begin(), (s).end()
namespace fs = std::filesystem;

enum TokenT { PlainText, SingleQuoted, Pipe, WhitespaceTk };

typedef struct Token {
  TokenT type;
  std::string text;
} Token;

std::ostream &operator<<(std::ostream &os, const Token &tok) {
  os << "TOKEN -> type: ";

  switch (tok.type) {
  case PlainText:
    os << "Plaintext, ";
    break;
  case SingleQuoted:
    os << "SingleQuoted, ";
    break;
  case Pipe:
    os << "Pipe, ";
    break;
  case WhitespaceTk:
    os << "WhitespaceTk, ";
    break;
  }

  os << "text: " << tok.text;
  return os;
}

enum TreeT { Builtin, ExecutableFile, TextNode, Leaf, WhitespaceNode };

typedef struct Tree {
  TreeT type;
  std::string value;
  fs::path path;
  std::vector<Tree> children;
} Tree;

std::ostream &operator<<(std::ostream &os, const Tree &t) {
  os << "{ type: ";

  switch (t.type) {
  case Builtin:
    os << "Builtin, ";
    break;
  case ExecutableFile:
    os << "ExecutableFile, ";
    break;
  case TextNode:
    os << "TextNode, ";
    break;
  case WhitespaceNode:
    os << "WhitespaceNode, ";
    break;
  case Leaf:
    os << "Leaf, ";
    break;
  }

  os << "value: " << t.value << ", children: ";
  for (auto &child : t.children) {
    os << child << ',';
  }

  os << " }";

  return os;
}

std::vector<std::filesystem::path> exec_files;
std::vector<std::string> builtins = {"cd", "exit", "echo", "pwd", "type"};

fs::path find_in_path(std::string s);

bool peek(const std::string &s, int (*f)(int), int pos) {
  if (pos + 1 < s.size()) {
    return f(s[pos + 1]);
  }

  return false;
}

bool peek(const std::string &s, bool (*f)(char), int pos) {
  if (pos + 1 < s.size()) {
    return f(s[pos + 1]);
  }

  return false;
}

std::vector<Token> parse(std::string in) {
  std::vector<Token> tokens;

  int prev = 0;
  for (int i = 0; i < in.size(); i++, prev = i) {
    while (isspace(in[i])) {
      i++;
    }

    if (i > prev && tokens.size() > 1) {
      tokens.emplace_back(Token{WhitespaceTk, " "});
    }

    switch (in[i]) {
    case '\'': {
      i++;
      int prev = i;

      auto cond = [](char c) { return c != '\''; };
      while (peek(in, cond, i))
        i++;

      tokens.emplace_back(Token{SingleQuoted, in.substr(prev, i - prev + 1)});
    } break;

    default: {
      int prev = i;

      auto cond = [](char c) { return isalnum(c) || c == '_'; };
      while (peek(in, cond, i))
        i++;

      std::string s = in.substr(prev, i - prev + 1);

      tokens.emplace_back(Token{PlainText, s});
    } break;
    }
  }

  return tokens;
}

Tree check(std::vector<Token> &tokens) {
  Tree tree{Leaf, "", {}};

  for (int i = 0; i < tokens.size(); i++) {
    const Token *cur = &tokens[i];

    switch (cur->type) {
    case PlainText:
    case SingleQuoted: {
      Tree node;
      if (std::find(ALL(builtins), cur->text) != builtins.end()) {
        node = {Builtin, cur->text, {}};
      } else {
        auto p = find_in_path(cur->text);
        if (!p.empty()) {
          node = {ExecutableFile, cur->text, p, {}};
        } else {
          node = {TextNode, cur->text, {}};
        }
      }

      if (tree.type != Leaf)
        tree.children.emplace_back(node);
      else
        tree = node;
    } break;
    case WhitespaceTk:
      tree.children.emplace_back(Tree{WhitespaceNode, cur->text, {}});
      break;
    case Pipe:
      break;
    }
  }

  return tree;
}

fs::path find_in_path(std::string s) {
  for (const auto &p : exec_files) {
    if (p.stem() == s) {
      return p;
    }
  }

  return fs::path{};
}

void chdir(std::string dir) {
  if (fs::exists(dir)) {
    fs::current_path(dir);
  } else if (dir == "~") {
    const char *home = std::getenv("HOME");

    fs::current_path(home);
  } else {
    std::cout << "cd: " << dir << ": No such file or directory" << std::endl;
  }
}

void execute(const Tree &ast) {
  switch (ast.type) {
  case Builtin: {
    if (ast.value == "cd") {
      if (ast.children.size() == 1)
        chdir(ast.children[1].value);
      else if (ast.children.size() < 1)
        std::cout << "Too many args for cd command";

    } else if (ast.value == "echo") {
      for (const auto &child : ast.children) {
        std::cout << child.value;
      }

      std::cout << std::endl;
    } else if (ast.value == "exit") {
      exit(0);
    } else if (ast.value == "pwd") {
      std::cout << fs::current_path().c_str() << std::endl;
    } else if (ast.value == "type") {

      for (const auto &child : ast.children) {

        if (child.type == Builtin) {
          std::cout << child.value << " is a shell builtin" << std::endl;
        } else if (child.type == ExecutableFile) {

          std::cout << child.value << " is "
                    << child.path.parent_path().string() << '/' << child.value
                    << std::endl;
        } else {
          std::cout << child.value << ": not found" << std::endl;
        }
      }
    }
  } break;
  case ExecutableFile: {

  } break;
  default:
    std::cout << ast.value << ": command not found" << std::endl;
    break;
  }
}

int main() {
  const char *tmp = std::getenv("PATH");
  std::string path(tmp ? tmp : "");
  path.append(":");

  int prev = 0;
  for (int i = 0; i < path.size(); i++) {
    if (path[i] == ':') {
      std::string p = path.substr(prev, i - prev);

      if (fs::exists(p)) {
        for (const auto &entry : fs::directory_iterator(p)) {
          // ファイル名一覧を保存
          if (!entry.is_directory() && (entry.status().permissions() & fs::perms::owner_exec) == fs::perms::owner_exec) {
            exec_files.emplace_back(entry.path());
          }
        }
      }

      prev = i + 1;
    }
  }

  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  bool exit = false;

  while (!exit) {
    std::cout << "$ ";

    std::string input;
    std::getline(std::cin, input);

    std::istringstream iss(input);

    std::vector<Token> tokens = parse(input);

    /*for (auto &tok : tokens)*/
    /*  std::cout << tok << std::endl;*/

    Tree ast = check(tokens);

    /*std::cout << ast << std::endl;*/
    execute(ast);

    /*fs::path p = find_in_path(words[0]);*/
    /**/
    /*if (!p.empty())*/
    /*  system(input.c_str());*/
    /*else*/
    /*  std::cout << words[0] << ": command not found" << std::endl;*/
  }
}