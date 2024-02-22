#include "mutter_remote_desktop.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib-unix.h>
#include <glob.h>
#include <pipewire/pipewire.h>
#include <poll.h>
#include <pthread.h>
#include <libevdev/libevdev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// mapping of mouse ecodes to remotedesktop keycodes
int mouse_event_code_to_gnome_keycode_map[] = {
    [BTN_MOUSE] = 272,
    [BTN_LEFT] = 272,
    [BTN_RIGHT] = 273,
    [BTN_MIDDLE] = 274,
    [BTN_TOUCH] = 272,
    [BTN_TL] = 272,
    [BTN_TR] = 273,
};

struct RemoteDesktopHandler {
    OrgGnomeMutterRemoteDesktopSession *remote_desktop_session;
    const char *stream_path;
    float last_rel_x;
    float last_rel_y;
    float last_touchpad_x;
    float last_touchpad_y;
};

bool reset_input_thread = false;
int mouse_count = 0;
struct libevdev** mouse_devices;

int keyboard_count = 0;
struct libevdev** keyboard_devices;

int input_device_count = 0;
struct libevdev** input_devices;

// iterate over event files looking for mouse/keyboard devices
void find_input_devices() {
    glob_t globbuf;
    glob("/dev/input/event*", GLOB_ERR, NULL, &globbuf);
    struct libevdev* source_dev = NULL;
    const char* source_dev_path;
    bool found_steam_deck = false;
    for(int i=0; i < globbuf.gl_pathc; i++) {
        const char* device_path = globbuf.gl_pathv[i];
        int fd = open(device_path, O_RDONLY | O_NONBLOCK);
        if (fd > 0) {
            struct libevdev* evdev;
            const int rc = libevdev_new_from_fd(fd, &evdev);
            if (rc >= 0) {
                // make sure it's not the virtual device we've created
                bool input_device = false;
                if (libevdev_has_event_code(evdev, EV_KEY, BTN_MOUSE) ||
                    libevdev_has_event_code(evdev, EV_KEY, BTN_TOUCH)) {
                    mouse_devices = realloc(mouse_devices, sizeof(struct libevdev*) * (mouse_count+1));
                    mouse_devices[mouse_count++] = evdev;
                    input_device = true;
                } else if (libevdev_has_event_code(evdev, EV_KEY, KEY_F1)) {
                    keyboard_devices = realloc(keyboard_devices, sizeof(struct libevdev*) * (keyboard_count+1));
                    keyboard_devices[keyboard_count++] = evdev;
                    input_device = true;
                } else {
                    libevdev_free(evdev);
                    close(fd);
                }

                if (input_device) {
                    input_devices = realloc(input_devices, sizeof(struct libevdev*) * (input_device_count+1));
                    input_devices[input_device_count++] = evdev;
                }
            } else {
                fprintf(stdout, "\tdebug: unable to open as evdev: %s\n", device_path);
            }
        } else {
            fprintf(stdout, "\tdebug: unable to open: %s\n", device_path);
        }
    }
}

void handle_event(struct RemoteDesktopHandler *handler, struct input_event event, bool is_mouse) {
    if (is_mouse) {
        if (event.type == EV_REL) {
            if (event.code == REL_X) {
                handler->last_rel_x = (double) event.value;
            } else if (event.code == REL_Y) {
                handler->last_rel_y = (double) event.value;
            } else if (event.code == REL_WHEEL) {
                org_gnome_mutter_remote_desktop_session_call_notify_pointer_axis_sync(
                    handler->remote_desktop_session,
                    0,
                    -event.value,
                    2, // mouse wheel flag
                    NULL, NULL);
            } else if (event.code == REL_HWHEEL) {
                org_gnome_mutter_remote_desktop_session_call_notify_pointer_axis_sync(
                    handler->remote_desktop_session,
                    event.value,
                    0,
                    2, // mouse wheel flag
                    NULL, NULL);
            }
        } else if (event.type == EV_ABS) {
            if (event.code == ABS_X) {
                handler->last_touchpad_x = event.value;
            } else if (event.code == ABS_Y) {
                handler->last_touchpad_y = event.value;
            }
        } else if (event.type == EV_KEY) {
            int keycode = mouse_event_code_to_gnome_keycode_map[event.code];
            if (keycode) {
                org_gnome_mutter_remote_desktop_session_call_notify_pointer_button_sync(
                    handler->remote_desktop_session,
                    keycode,
                    event.value,
                    NULL, NULL);
            }
        } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
            if (handler->last_rel_x != 0 || handler->last_rel_y != 0) {
                org_gnome_mutter_remote_desktop_session_call_notify_pointer_motion_relative_sync(
                    handler->remote_desktop_session,
                    handler->last_rel_x,
                    handler->last_rel_y,
                    NULL, NULL);
                handler->last_rel_x = 0;
                handler->last_rel_y = 0;
            }

            if (handler->last_touchpad_x != -1 && handler->last_touchpad_y != -1) {
                org_gnome_mutter_remote_desktop_session_call_notify_pointer_motion_absolute_sync(
                    handler->remote_desktop_session,
                    "breezy",
                    handler->last_touchpad_x,
                    handler->last_touchpad_y,
                    NULL, NULL);
                handler->last_touchpad_x = -1;
                handler->last_touchpad_y = -1;
            }
        }
    } else {
        if (event.type == EV_KEY) {
            org_gnome_mutter_remote_desktop_session_call_notify_keyboard_keycode_sync(
                handler->remote_desktop_session,
                event.code,
                event.value,
                NULL, NULL);
        }
    }
}

bool in_array(struct libevdev* dev, struct libevdev** array, int count) {
    for (int i=0; i<count; i++) {
        if (array[i] == dev) {
            return true;
        }
    }
    return false;
}

// pthread function to poll input devices and copy their events to our remote desktop session
void *poll_input_devices(void *arg) {
    struct RemoteDesktopHandler *handler = (struct RemoteDesktopHandler *)arg;
    if (input_device_count > 0) {
        // don't exit this thread unless the driver becomes disabled or the glasses are disconnected
        while(!reset_input_thread) {
            // do this on every iteration of the first while loop so that if poll times out (returns 0), we can reset
            // our fds in case those have changed
            struct pollfd* fds = malloc(sizeof(*fds)*input_device_count);
            for (int i=0; i < input_device_count; i++) {
                struct libevdev* input_evdev = input_devices[i];
                fds[i].fd = libevdev_get_fd(input_evdev);
                fds[i].events = POLLIN;
            }

            while (!reset_input_thread && poll(fds, input_device_count, 1000)) {
                for (int i=0;i<input_device_count;i++) {
                    if (fds[i].revents & POLLIN) {
                        struct libevdev* input_evdev = input_devices[i];
                        struct input_event event;
                        if (!libevdev_has_event_pending(input_evdev)) continue;

                        bool is_mouse = in_array(input_evdev, mouse_devices, mouse_count);

                        int next_event_read_flag = LIBEVDEV_READ_FLAG_NORMAL;
                        int next_event_status = libevdev_next_event(input_evdev, next_event_read_flag, &event);

                        while (next_event_status == LIBEVDEV_READ_STATUS_SYNC || next_event_status == LIBEVDEV_READ_STATUS_SUCCESS) {
                            handle_event(handler, event, is_mouse);

                            if (next_event_status == LIBEVDEV_READ_STATUS_SYNC) {
                                next_event_read_flag =  next_event_status == LIBEVDEV_READ_STATUS_SYNC ? 
                                                            LIBEVDEV_READ_FLAG_SYNC :
                                                            LIBEVDEV_READ_FLAG_NORMAL;
                            }

                            next_event_status = libevdev_next_event(input_evdev, next_event_read_flag, &event);
                        }
                    } else if (fds[i].revents & (POLLHUP | POLLERR)) {
                        printf("input device was disconnected, resetting devices\n");
                        reset_input_thread = true;
                    }
                }
            }

            free(fds);
        }
    }

    printf("\tdebug: Exiting poll_input_devices thread\n");
}

void remote_desktop_inputs_init(
    OrgGnomeMutterRemoteDesktopSession *remote_desktop_session,
    const char *stream_path) {
    struct RemoteDesktopHandler handler = {
        .remote_desktop_session = remote_desktop_session,
        .stream_path = stream_path,
        .last_touchpad_x = -1,
        .last_touchpad_y = -1,
    };

    find_input_devices();

    pthread_t input_thread;
    pthread_create(&input_thread, NULL, poll_input_devices, &handler);
}