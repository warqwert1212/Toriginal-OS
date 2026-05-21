#include "gui_framework.h"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace std;

/* Desktop singleton */
Desktop* Desktop::instance = nullptr;

Desktop& Desktop::getInstance() {
    if (instance == nullptr) {
        instance = new Desktop();
    }
    return *instance;
}

Desktop::Desktop() {}

void Desktop::initialize() {
    system("clear");
    cout << BG_CYAN << COLOR_BLACK;
    cout << string(SCREEN_WIDTH, ' ') << COLOR_RESET << endl;
}

void Desktop::render() {
    system("clear");
    
    /* Render wallpaper */
    cout << BG_CYAN;
    for (int y = 0; y < SCREEN_HEIGHT - 2; y++) {
        cout << COLOR_BLACK << string(SCREEN_WIDTH, '~') << COLOR_RESET << endl;
    }
    cout << COLOR_RESET;
    
    /* Render windows */
    for (auto window : windows) {
        window->render();
    }
    
    /* Render taskbar */
    cout << COLOR_BLUE << BG_BLACK;
    cout << "\033[" << SCREEN_HEIGHT << ";0H";  /* Move to bottom */
    cout << string(SCREEN_WIDTH, '═') << COLOR_RESET << endl;
    cout << "🪟 Toriginal OS | ";
    for (auto &app : windows) {
        cout << "[" << app->title << "] ";
    }
    cout << endl;
}

void Desktop::addWindow(Window *window) {
    windows.push_back(window);
}

void Desktop::removeWindow(Window *window) {
    windows.erase(remove(windows.begin(), windows.end(), window), windows.end());
}

void Desktop::handleInput(char key) {
    if (!windows.empty() && windows.back()->active) {
        windows.back()->handleInput(key);
    }
}

void Desktop::setWallpaper(const string &path) {
    wallpaper = path;
}

/* Window implementation */
Window::Window(const string &t, int x, int y, int w, int h)
    : title(t), x(x), y(y), width(w), height(h), active(true) {}

Window::~Window() {}

void Window::render() {
    /* Move to position */
    cout << "\033[" << y << ";" << x << "H";
    
    drawBorder();
    drawTitle();
}

void Window::drawBorder() {
    cout << COLOR_WHITE << BG_BLUE;
    
    /* Top border */
    cout << "╔";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╗" << endl;
    
    /* Side borders */
    for (int i = 0; i < height - 2; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    
    /* Bottom border */
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void Window::drawTitle() {
    cout << "\033[" << (y + 0) << ";" << (x + 2) << "H";
    cout << COLOR_WHITE << title << COLOR_RESET;
}

void Window::handleInput(char key) {
    /* Override in subclasses */
}

void Window::setActive(bool active) {
    this->active = active;
}

/* Button implementation */
Button::Button(const string &l, int x, int y)
    : label(l), x(x), y(y), focused(false) {}

Button::~Button() {}

void Button::render() {
    cout << "\033[" << y << ";" << x << "H";
    
    if (focused) {
        cout << COLOR_WHITE << BG_BLACK << "[ " << label << " ]" << COLOR_RESET;
    } else {
        cout << COLOR_CYAN << "[ " << label << " ]" << COLOR_RESET;
    }
}

/* Label implementation */
Label::Label(const string &t, int x, int y, const string &c)
    : text(t), x(x), y(y), color(c) {}

Label::~Label() {}

void Label::render() {
    cout << "\033[" << y << ";" << x << "H";
    cout << color << text << COLOR_RESET;
}

/* TextBox implementation */
TextBox::TextBox(int x, int y, int w)
    : x(x), y(y), width(w), focused(false) {}

TextBox::~TextBox() {}

void TextBox::render() {
    cout << "\033[" << y << ";" << x << "H";
    
    if (focused) {
        cout << COLOR_WHITE << BG_BLACK << "┌" << string(width - 2, '─') << "┐" << endl;
        cout << "│" << content << string(width - 2 - content.length(), ' ') << "│" << endl;
        cout << "└" << string(width - 2, '─') << "┘" << COLOR_RESET;
    } else {
        cout << COLOR_CYAN << "┌" << string(width - 2, '─') << "┐" << endl;
        cout << "│" << content << string(width - 2 - content.length(), ' ') << "│" << endl;
        cout << "└" << string(width - 2, '─') << "┘" << COLOR_RESET;
    }
}

/* TaskBar implementation */
TaskBar::TaskBar() {}

TaskBar::~TaskBar() {}

void TaskBar::render() {
    cout << "\033[" << SCREEN_HEIGHT << ";0H";
    cout << COLOR_BLUE << BG_BLACK << "╔";
    cout << string(SCREEN_WIDTH - 2, '═') << "╗" << COLOR_RESET << endl;
    cout << COLOR_BLUE << BG_BLACK << "║ Start 📁 ";
    
    for (auto &app : apps) {
        cout << "[" << app << "] ";
    }
    
    cout << string(SCREEN_WIDTH - 20 - (apps.size() * 6), ' ') << "║" << COLOR_RESET << endl;
}

void TaskBar::addApp(const string &name) {
    apps.push_back(name);
}

void TaskBar::handleInput(char key) {
    /* Start menu functionality */
}
