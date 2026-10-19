#include <flutter_linux/flutter_linux.h>

// Exercise the initialization that must not emit any GTK or ATK critical.
int main(int argc, char** argv) {
  gtk_init(&argc, &argv);
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  FlView* view = fl_view_new(project);
  g_assert_nonnull(fl_view_get_engine(view));
  g_object_ref_sink(view);
  gtk_widget_destroy(GTK_WIDGET(view));
  g_object_unref(view);
  g_print("Flutter view initialization passed with fatal criticals enabled.\n");
  return 0;
}
