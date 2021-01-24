
#ifndef EVENT_H
#define EVENT_H



#define EVENT_NONE          0
#define EVENT_RESIZE        1
#define EVENT_MOUSEWHEEL    2
#define EVENT_MOUSEPRESS    3
#define EVENT_MOUSERELEASE  4
#define EVENT_MOUSEMOVED    5

struct resize_t {
  int width;
  int height;
};

struct mouse_wheel_t {
  int delta;
};

struct mouse_press_t {
  int x;
  int y;
  int button;
  int clicks;
};

struct mouse_release_t {
  int x;
  int y;
  int button;
};

struct mouse_moved_t {
  int x;
  int y;
  int xrel;
  int yrel;
};

typedef struct event_t {
  int type;
  union {
    struct resize_t        resize;
    struct mouse_wheel_t   mousewheel;
    struct mouse_press_t   mousepress;
    struct mouse_release_t mouserelease;
    struct mouse_moved_t   mousemoved;
  };
} event_t;


int event_has(void);
void event_push(event_t event);
event_t event_pop(void);

#endif

