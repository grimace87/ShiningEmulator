#include "menu.h"

Menu::Menu(const wchar_t* text, Action action) : text{ text }, action{ action } { }

Menu::Menu(const wchar_t* text, std::initializer_list<Menu> subMenus) :
        text{ text },
        action{ Action::MSG_UNUSED },
        subMenus { subMenus } { }

Menu Menu::buildMain() {
    // Build main menu
    return Menu {
            L"", {
                    Menu {L"Things", {
                        Menu {L"Open something", Action::MSG_OPEN_FILE},
                        Menu {L"Stuff", Action::MSG_UNUSED}
                    }},
                    Menu {L"Exit", Action::MSG_EXIT}
            }
    };
}
