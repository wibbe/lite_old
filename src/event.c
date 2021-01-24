
#include "event.h"


static event_t event_queue[16];
static int head;
static int tail;
static event_t nil_event = {0};




int event_has(void) {
  return head != tail;
}

void event_push(event_t event) {
  event_queue[head] = event;
  head = (head + 1) % 16;
}

event_t event_pop(void) {
  if (head == tail)
    return nil_event;

  int old_tail = tail;
  tail = (tail + 1) % 16;
  return event_queue[old_tail];
}

