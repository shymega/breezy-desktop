#include "mutter_remote_desktop.h"
#include "mutter_screen_cast.h"
#include "pipewire.h"
#include "remote_desktop_inputs.h"

#include <assert.h>
#include <pipewire/pipewire.h>
#include <libplacebo/config.h>
#include <libplacebo/renderer.h>
#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <gio/gio.h>
#include <glib.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <signal.h>

#define WIDTH 1920
#define HEIGHT 1080

pl_log pllog;
SDL_Window *win;
struct pl_context *ctx;
struct pl_renderer *renderer;
const struct pl_vulkan *vk;
const struct pl_vk_inst *vk_inst;
VkSurfaceKHR surf;
const struct pl_gpu *gpu;
const struct pl_swapchain *swapchain;
SDL_mutex *lock;
struct pl_frame image;
const struct pl_tex *plane_tex[3];

void segfault_handler(int signal, siginfo_t *si, void *arg)
{
    void *error_addr = si->si_addr;

    // Write the error address to stderr
    fprintf(stderr, "Segmentation fault occurred at address: %p\n", error_addr);

    // Write the backtrace to stderr
    void *buffer[10];
    int nptrs = backtrace(buffer, 10);
    backtrace_symbols_fd(buffer, nptrs, 2);

    // End the process
    exit(EXIT_FAILURE);
}

static SDL_Window *create_window() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return NULL;
    }

    win = SDL_CreateWindow(
        "Breezy Desktop",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH,
        HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS
    );

    if (win == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return NULL;
    }

    return win;
}

void setup_vulkan() {
    ctx = pl_context_create(PL_API_VER, NULL);

    // Init Vulkan
    unsigned num = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(win, &num, NULL)) {
        fprintf(stderr, "Failed enumerating Vulkan extensions: %s\n", SDL_GetError());
        exit(1);
    }

    const char **extensions = malloc(num * sizeof(const char *));
    assert(extensions);

    SDL_bool ok = SDL_Vulkan_GetInstanceExtensions(win, &num, extensions);
    if (!ok) {
        fprintf(stderr, "Failed getting Vk instance extensions\n");
        exit(1);
    }

    if (num > 0) {
        printf("Requesting %d additional Vulkan extensions:\n", num);
        for (unsigned i = 0; i < num; i++)
            printf("    %s\n", extensions[i]);
    }

    struct pl_vk_inst_params iparams = { 0 };
    iparams.extensions = extensions;
    iparams.num_extensions = num;

    vk_inst = pl_vk_inst_create(ctx, &iparams);
    if (!vk_inst) {
        fprintf(stderr, "Failed creating Vulkan instance!\n");
        exit(1);
    }
    free(extensions);

    if (!SDL_Vulkan_CreateSurface(win, vk_inst->instance, &surf)) {
        fprintf(stderr, "Failed creating vulkan surface: %s\n", SDL_GetError());
        exit(1);
    }

    struct pl_vulkan_params params = pl_vulkan_default_params;
    params.instance = vk_inst->instance;
    params.surface = surf;
    params.allow_software = true;

    vk = pl_vulkan_create(ctx, &params);
    if (!vk) {
        fprintf(stderr, "Failed creating vulkan device!\n");
        exit(2);
    }

    // Create swapchain
    swapchain = pl_vulkan_create_swapchain(vk,
        &(struct pl_vulkan_swapchain_params) {
            .surface = surf,
            .present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR,
        });

    if (!swapchain) {
        fprintf(stderr, "Failed creating vulkan swapchain!\n");
        exit(2);
    }

    int w = WIDTH, h = HEIGHT;
    if (!pl_swapchain_resize(swapchain, &w, &h)) {
        fprintf(stderr, "Failed resizing vulkan swapchain!\n");
        exit(2);
    }

    gpu = vk->gpu;

    if (w != WIDTH || h != HEIGHT)
        printf("Note: window dimensions differ (got %dx%d)\n", w, h);

    renderer = pl_renderer_create(ctx, gpu);
}



SDL_mutex *lock;
static void on_process(void *userdata) {
    SDL_LockMutex(lock);
    printf("on_process\n");

    struct pl_swapchain_frame frame;
    bool ok = pl_swapchain_start_frame(swapchain, &frame);
    if (!ok) {
        SDL_UnlockMutex(lock);
        return;
    }

    struct pl_frame target;
    pl_frame_from_swapchain(&target, &frame);

    if (!pl_render_image(renderer, &image, &target, &pl_render_fast_params)) {
        fprintf(stderr, "Failed rendering frame!\n");
        pl_tex_clear(gpu, frame.fbo, (float[4]){ 1.0 });
    }

    ok = pl_swapchain_submit_frame(swapchain);
    if (!ok) {
        fprintf(stderr, "Failed submitting frame!\n");
        SDL_UnlockMutex(lock);
        return;
    }

    pl_swapchain_swap_buffers(swapchain);

    SDL_UnlockMutex(lock);
}

void on_pipewire_stream_added(OrgGnomeMutterScreenCastStream *stream, guint node_id, gpointer user_data) {
    g_print("PipeWire stream added, node id: %u\n", node_id);

    win = create_window();
    setup_vulkan();

    lock = SDL_CreateMutex();

    pw_setup(node_id);
}

int main() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);


    pllog = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb = pl_log_color,
        .log_level = PL_LOG_INFO,
    ));

    GMainLoop *loop;
    GVariant *result;
    gchar *screen_case_session_path;

    loop = g_main_loop_new(NULL, FALSE);

    GError *error = NULL;

    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (bus == NULL) {
        g_printerr("Failed to connect to the D-Bus session bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    OrgGnomeMutterRemoteDesktop *remote_desktop_proxy = org_gnome_mutter_remote_desktop_proxy_new_sync(
        bus,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.gnome.Mutter.RemoteDesktop",
        "/org/gnome/Mutter/RemoteDesktop",
        NULL, // cancellable
        &error
    );

    if (remote_desktop_proxy == NULL) {
        g_printerr("Failed to create proxy for org.gnome.Mutter.RemoteDesktop: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    OrgGnomeMutterScreenCast *screen_cast_proxy = org_gnome_mutter_screen_cast_proxy_new_sync(
        bus,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.gnome.Mutter.ScreenCast",
        "/org/gnome/Mutter/ScreenCast",
        NULL, // cancellable
        &error
    );

    if (screen_cast_proxy == NULL) {
        g_printerr("Failed to create proxy for org.gnome.Mutter.ScreenCast: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    // Call the CreateSession method
    gchar *session_object_path = NULL;
    gboolean success = org_gnome_mutter_remote_desktop_call_create_session_sync(
        remote_desktop_proxy,
        &session_object_path,
        NULL, // cancellable
        &error
    );

    if (!success) {
        g_printerr("Failed to create remote desktop session: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    g_print("session object path: %s\n", session_object_path);

    printf("here 2\n");
    OrgGnomeMutterRemoteDesktopSession *remote_desktop_session;
    remote_desktop_session = org_gnome_mutter_remote_desktop_session_proxy_new_sync(
        bus,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.gnome.Mutter.RemoteDesktop",
        session_object_path,
        NULL, // cancellable
        &error
    );

    if (remote_desktop_session == NULL) {
        g_printerr("Failed to create proxy for org.gnome.Mutter.RemoteDesktop.Session: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    const gchar *remote_desktop_session_id;
    remote_desktop_session_id = org_gnome_mutter_remote_desktop_session_get_session_id(remote_desktop_session);

    g_print("remote desktop session id: %s\n", remote_desktop_session_id);

    // Create a new ScreenCast session
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "remote-desktop-session-id", g_variant_new_string(remote_desktop_session_id));
    GVariant *parameters = g_variant_builder_end(&builder);
    gchar *screen_cast_session_path = NULL;
    success = org_gnome_mutter_screen_cast_call_create_session_sync(
        screen_cast_proxy,
        parameters,
        &screen_cast_session_path,
        NULL, // cancellable
        &error
    );

    if (!success) {
        g_printerr("Failed to create screen cast session: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    g_print("session path: %s\n", screen_cast_session_path);

    // Create a new proxy for the org.gnome.Mutter.ScreenCast.Session interface
    OrgGnomeMutterScreenCastSession *screen_cast_session;
    screen_cast_session = org_gnome_mutter_screen_cast_session_proxy_new_sync(
        bus,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.gnome.Mutter.ScreenCast",
        screen_cast_session_path,
        NULL, // cancellable
        &error
    );

    if (screen_cast_session == NULL) {
        g_printerr("Failed to create proxy for org.gnome.Mutter.ScreenCast.Session: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    // Call the RecordVirtual method
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "is-platform", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&builder, "{sv}", "cursor-mode", g_variant_new_uint32(1));
    parameters = g_variant_builder_end(&builder);
    gchar *stream_path = NULL;
    success = org_gnome_mutter_screen_cast_session_call_record_virtual_sync(
        screen_cast_session,
        parameters,
        &stream_path,
        NULL, // cancellable
        &error
    );

    if (!success) {
        g_printerr("Failed to record virtual: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    g_print("stream path: %s\n", stream_path);

    remote_desktop_inputs_init(remote_desktop_session, stream_path);

    // Create a new proxy for the org.gnome.Mutter.ScreenCast.Stream interface
    OrgGnomeMutterScreenCastStream *stream;
    stream = org_gnome_mutter_screen_cast_stream_proxy_new_sync(
        bus,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.gnome.Mutter.ScreenCast",
        stream_path,
        NULL, // cancellable
        &error
    );

    if (stream == NULL) {
        g_printerr("Failed to create proxy for org.gnome.Mutter.ScreenCast.Stream: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    g_signal_connect(stream, "pipewire-stream-added", G_CALLBACK(on_pipewire_stream_added), NULL);

    success = org_gnome_mutter_remote_desktop_session_call_start_sync(
        remote_desktop_session,
        NULL, // cancellable
        &error
    );

    g_main_loop_run(loop);

    // Clean up
    g_object_unref(bus);
    g_object_unref(remote_desktop_proxy);
    g_object_unref(screen_cast_proxy);
    g_object_unref(remote_desktop_session);
    g_object_unref(screen_cast_session);
    g_object_unref(stream);
    g_free(session_object_path);
    g_free(screen_cast_session_path);
    g_free(stream_path);

    pl_log_destroy(&pllog);
    return 0;
}
