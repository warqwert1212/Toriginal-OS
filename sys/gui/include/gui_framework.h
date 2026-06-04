#ifndef GUI_FRAMEWORK_H
#define GUI_FRAMEWORK_H

#include <string>
#include <vector>

/* Color definitions */
#define COLOR_BLACK     "\033[0;30m"
#define COLOR_RED       "\033[0;31m"
#define COLOR_GREEN     "\033[0;32m"
#define COLOR_YELLOW    "\033[0;33m"
#define COLOR_BLUE      "\033[0;34m"
#define COLOR_MAGENTA   "\033[0;35m"
#define COLOR_CYAN      "\033[0;36m"
#define COLOR_WHITE     "\033[0;37m"
#define COLOR_RESET     "\033[0m"

#define BG_BLACK        "\033[40m"
#define BG_BLUE         "\033[44m"
#define BG_CYAN         "\033[46m"

/* Screen dimensions */
#define SCREEN_WIDTH    120
#define SCREEN_HEIGHT   30

/* GUI Components */
class GUIComponent {
public:
    virtual ~GUIComponent() {}
    virtual void render() = 0;
};

class Window : public GUIComponent {
public:
    std::string title;
    int x, y, width, height;
    bool active;
    
    Window(const std::string &t, int x, int y, int w, int h);
    virtual ~Window();
    
    virtual void render();
    virtual void handleInput(char key);
    void setActive(bool active);
    
protected:
    void drawBorder();
    void drawTitle();
};

class Button : public GUIComponent {
public:
    std::string label;
    int x, y;
    bool focused;
    
    Button(const std::string &l, int x, int y);
    virtual ~Button();
    
    virtual void render();
    void setFocused(bool f) { focused = f; }
};

class Label : public GUIComponent {
public:
    std::string text;
    int x, y;
    std::string color;
    
    Label(const std::string &t, int x, int y, const std::string &c = COLOR_WHITE);
    virtual ~Label();
    
    virtual void render();
    void setText(const std::string &t) { text = t; }
};

class TextBox : public GUIComponent {
public:
    std::string content;
    int x, y, width;
    bool focused;
    
    TextBox(int x, int y, int w);
    virtual ~TextBox();
    
    virtual void render();
    void setText(const std::string &t) { content = t; }
    std::string getText() { return content; }
};

/* Desktop Manager */
class Desktop {
public:
    static Desktop& getInstance();
    
    void initialize();
    void render();
    void handleInput(char key);
    void addWindow(Window *window);
    void removeWindow(Window *window);
    void setWallpaper(const std::string &path);
    
private:
    Desktop();
    static Desktop *instance;
    std::vector<Window*> windows;
    std::string wallpaper;
};

/* TaskBar */
class TaskBar {
public:
    TaskBar();
    ~TaskBar();
    
    void render();
    void addApp(const std::string &name);
    void handleInput(char key);
    
private:
    std::vector<std::string> apps;
};

#endif /* GUI_FRAMEWORK_H */
