#include "platform.hpp"
#include <algorithm>
#include <gtk/gtk.h>
namespace paint {
namespace {
struct PrintState {
    GtkPageSetup* setup = nullptr;
    GtkPrintSettings* settings = nullptr;
    ~PrintState() {
        if (setup) {
            g_object_unref(setup);
        }
        if (settings) {
            g_object_unref(settings);
        }
    }
};
PrintState print_state;
} // namespace
void page_setup() {
    if (!gtk_init_check(nullptr, nullptr)) {
        return;
    }
    GtkPageSetup* replacement =
        gtk_print_run_page_setup_dialog(nullptr, print_state.setup, print_state.settings);
    if (replacement != print_state.setup) {
        if (print_state.setup) {
            g_object_unref(print_state.setup);
        }
        print_state.setup = replacement;
    }
}
static void draw_print_page(GtkPrintOperation*, GtkPrintContext* context, gint, gpointer userdata) {
    Image& image = *static_cast<Image*>(userdata);
    cairo_t* drawing = gtk_print_context_get_cairo_context(context);
    double page_width = gtk_print_context_get_width(context);
    double page_height = gtk_print_context_get_height(context);
    double scale = std::min(page_width / image.width, page_height / image.height);
    cairo_surface_t* surface =
        cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(image.pixels.data()),
                                            CAIRO_FORMAT_RGB24, image.width, image.height, image.width * 4);
    cairo_save(drawing);
    cairo_translate(drawing, (page_width - image.width * scale) / 2,
                    (page_height - image.height * scale) / 2);
    cairo_scale(drawing, scale, scale);
    cairo_set_source_surface(drawing, surface, 0, 0);
    cairo_paint(drawing);
    cairo_restore(drawing);
    cairo_surface_destroy(surface);
}
bool print_image(const Image& source) {
    if (!gtk_init_check(nullptr, nullptr)) {
        return false;
    }
    Image image;
    image.reset(source.width, source.height);
    composite(image, source, 0, 0);
    for (Color& pixel : image.pixels) {
        std::swap(pixel.r, pixel.b);
    }
    GtkPrintOperation* operation = gtk_print_operation_new();
    gtk_print_operation_set_job_name(operation, "Plan Paint picture");
    gtk_print_operation_set_n_pages(operation, 1);
    gtk_print_operation_set_allow_async(operation, FALSE);
    if (print_state.setup) {
        gtk_print_operation_set_default_page_setup(operation, print_state.setup);
    }
    if (print_state.settings) {
        gtk_print_operation_set_print_settings(operation, print_state.settings);
    }
    g_signal_connect(operation, "draw-page", G_CALLBACK(draw_print_page), &image);
    GtkPrintOperationResult result =
        gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, nullptr, nullptr);
    if (result == GTK_PRINT_OPERATION_RESULT_APPLY) {
        GtkPrintSettings* replacement = gtk_print_operation_get_print_settings(operation);
        if (replacement) {
            g_object_ref(replacement);
        }
        if (print_state.settings) {
            g_object_unref(print_state.settings);
        }
        print_state.settings = replacement;
    }
    g_object_unref(operation);
    return result == GTK_PRINT_OPERATION_RESULT_APPLY;
}
} // namespace paint
