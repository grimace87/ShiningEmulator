#pragma once

#include "messagedefs.h"

#include <initializer_list>
#include <string>
#include <vector>

class Menu {
    Menu(const wchar_t* text, Action action);
    Menu(const wchar_t* text, std::initializer_list<Menu> subMenus);

public:
    static Menu buildMain();
    std::wstring text;
    Action action;
    std::vector<Menu> subMenus;
};
