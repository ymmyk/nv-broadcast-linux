// nvbcast-gui: GTK4/libadwaita config editor + live meters for nvbcastd.
//
// Thin skin over nvb_common: device scan, TOML config, `systemctl --user`
// service control. Live levels come from the daemon's status file
// (see common/status.h); no PipeWire link needed in this process.
#include <adwaita.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/devices.h"
#include "common/status.h"

namespace {

struct Gui {
  GtkApplication* app = nullptr;
  GtkWidget* window = nullptr;
  AdwToastOverlay* toasts = nullptr;
  GtkComboBoxText* input_combo = nullptr;
  GtkEditable* output_entry = nullptr;
  GtkComboBoxText* backend_combo = nullptr;
  GtkRange* suppression = nullptr;
  GtkRange* vad = nullptr;
  GtkRange* keyboard = nullptr;
  GtkLabel* svc_label = nullptr;
  GtkLevelBar* in_meter = nullptr;
  GtkLevelBar* out_meter = nullptr;
  GtkLabel* counters = nullptr;
  GtkSwitch* login_switch = nullptr;
  std::string cfg_path;
  nvb::AppConfig cfg;
  std::vector<std::string> device_ids;  // parallel to input_combo rows
};

std::string RunQuiet(const std::string& cmd) {
  std::string out;
  char buf[256];
  FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p) return out;
  while (fgets(buf, sizeof buf, p)) out += buf;
  pclose(p);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    out.pop_back();
  return out;
}

bool SvcActive() {
  return system("systemctl --user is-active --quiet nv-broadcast.service") == 0;
}
bool SvcEnabled() {
  return system("systemctl --user is-enabled --quiet nv-broadcast.service") == 0;
}

void Toast(Gui* g, const char* msg) {
  adw_toast_overlay_add_toast(g->toasts, adw_toast_new(msg));
}

void RefreshDevices(Gui* g) {
  gtk_combo_box_text_remove_all(g->input_combo);
  g->device_ids.clear();
  gtk_combo_box_text_append_text(g->input_combo, "<default source>");
  g->device_ids.emplace_back("");
  int active = 0;
  for (auto& d : nvb::ScanInputDevices()) {
    // Button shows the short human label only: the full "id — label" string
    // forces a huge minimum width, squeezing the row title into one column
    // of characters. Full ids stay in device_ids[] for saving.
    std::string label = d.label.empty() ? d.id : d.label;
    if (label.size() > 44) label = label.substr(0, 43) + "…";
    gtk_combo_box_text_append_text(g->input_combo, label.c_str());
    g->device_ids.push_back(d.id);
    if (d.id == g->cfg.input_device) active = (int)g->device_ids.size() - 1;
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(g->input_combo), active);
}

void LoadToWidgets(Gui* g) {
  gtk_editable_set_text(g->output_entry, g->cfg.output_name.c_str());
  gtk_combo_box_set_active(
      GTK_COMBO_BOX(g->backend_combo),
      g->cfg.backend == "maxine" ? 1 : (g->cfg.backend == "cuda" ? 2 : 0));
  gtk_range_set_value(g->suppression, g->cfg.suppression_db);
  gtk_range_set_value(g->vad, g->cfg.vad_threshold);
  gtk_range_set_value(g->keyboard, g->cfg.keyboard_boost);
  RefreshDevices(g);
}

void SaveFromWidgets(Gui* g) {
  int in = gtk_combo_box_get_active(GTK_COMBO_BOX(g->input_combo));
  g->cfg.input_device =
      (in > 0 && in < (int)g->device_ids.size()) ? g->device_ids[in] : "";
  g->cfg.output_name = gtk_editable_get_text(g->output_entry);
  int be = gtk_combo_box_get_active(GTK_COMBO_BOX(g->backend_combo));
  g->cfg.backend = be == 1 ? "maxine" : (be == 2 ? "cuda" : "rnnoise");
  g->cfg.suppression_db = (float)gtk_range_get_value(g->suppression);
  g->cfg.vad_threshold = (float)gtk_range_get_value(g->vad);
  g->cfg.keyboard_boost = (float)gtk_range_get_value(g->keyboard);
  std::string err;
  if (g->cfg.save(g->cfg_path, &err))
    Toast(g, "Saved — restart the daemon to apply input/backend changes");
  else
    Toast(g, ("Save failed: " + err).c_str());
}

gboolean PollStatus(gpointer data) {
  Gui* g = static_cast<Gui*>(data);
  nvb::DaemonStatus st;
  long age = 0;
  bool live = nvb::ReadStatus(nvb::StatusPath(), &st, &age) && st.running &&
              age < 2000;
  bool svc = SvcActive();
  gtk_label_set_text(
      g->svc_label,
      live ? "● Running" : (svc ? "○ Service active, no signal yet" : "○ Stopped"));
  gtk_level_bar_set_value(g->in_meter, live && st.in_peak < 1 ? st.in_peak : 1);
  gtk_level_bar_set_value(g->out_meter,
                          live && st.out_peak < 1 ? st.out_peak : 1);
  if (live) {
    char buf[128];
    snprintf(buf, sizeof buf, "captured %lu · rendered %lu · dropped %lu",
             (unsigned long)st.captured, (unsigned long)st.rendered,
             (unsigned long)st.dropped);
    gtk_label_set_text(g->counters, buf);
  }
  return G_SOURCE_CONTINUE;
}

GtkWidget* ScaleRow(Gui* g, AdwPreferencesGroup* group, const char* title,
                    const char* subtitle, double min, double max, double step,
                    int digits, GtkRange** out) {
  AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  adw_action_row_set_subtitle(row, subtitle);
  GtkWidget* scale =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
  gtk_scale_set_digits(GTK_SCALE(scale), digits);
  gtk_widget_set_size_request(scale, 180, -1);
  gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(row, scale);
  adw_preferences_group_add(group, GTK_WIDGET(row));
  *out = GTK_RANGE(scale);
  (void)g;
  return scale;
}

void OnActivate(GtkApplication* app, gpointer data) {
  Gui* g = static_cast<Gui*>(data);
  g->app = app;

  g->window = adw_application_window_new(GTK_APPLICATION(app));
  gtk_window_set_title(GTK_WINDOW(g->window), "NV Broadcast");
  gtk_window_set_default_size(GTK_WINDOW(g->window), 480, 660);

  g->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
  // AdwApplicationWindow forbids gtk_window_set_child(); content goes here.
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(g->window),
                                     GTK_WIDGET(g->toasts));

  AdwPreferencesPage* page =
      ADW_PREFERENCES_PAGE(adw_preferences_page_new());
  adw_toast_overlay_set_child(g->toasts, GTK_WIDGET(page));

  // ---- Devices ----
  AdwPreferencesGroup* dev =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(dev, "Devices");
  adw_preferences_page_add(page, dev);

  AdwActionRow* inrow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(inrow), "Filter input");
  adw_action_row_set_subtitle(inrow, "Microphone to denoise");
  g->input_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  gtk_widget_set_valign(GTK_WIDGET(g->input_combo), GTK_ALIGN_CENTER);
  // Cap the combo's width so long device names can't steal the row:
  // ellipsize the button text instead of wrapping the whole row.
  gtk_widget_set_size_request(GTK_WIDGET(g->input_combo), 280, -1);
  {
    GList* cells =
        gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(g->input_combo));
    for (GList* l = cells; l; l = l->next)
      g_object_set(l->data, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_list_free(cells);
  }
  adw_action_row_add_suffix(inrow, GTK_WIDGET(g->input_combo));
  adw_preferences_group_add(dev, GTK_WIDGET(inrow));

  GtkWidget* refresh = gtk_button_new_with_label("Rescan");
  g_signal_connect_swapped(refresh, "clicked", G_CALLBACK(RefreshDevices), g);
  AdwActionRow* rrow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(rrow), "Devices");
  adw_action_row_add_suffix(rrow, refresh);
  adw_preferences_group_add(dev, GTK_WIDGET(rrow));

  AdwEntryRow* outrow = ADW_ENTRY_ROW(adw_entry_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(outrow), "Virtual mic name");
  // NB: AdwEntryRow is NOT an AdwActionRow (both share AdwPreferencesRow),
  // so no subtitle call here — that cast aborts. Title only.
  g->output_entry = GTK_EDITABLE(outrow);  // AdwEntryRow implements GtkEditable
  adw_preferences_group_add(dev, GTK_WIDGET(outrow));

  // ---- Denoise ----
  AdwPreferencesGroup* den =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(den, "Denoise");
  adw_preferences_page_add(page, den);

  AdwActionRow* brow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(brow), "Backend");
  adw_action_row_set_subtitle(brow, "cuda = experimental GPU stub");
  g->backend_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  gtk_combo_box_text_append_text(g->backend_combo, "rnnoise");
  gtk_combo_box_text_append_text(g->backend_combo, "maxine");
  gtk_combo_box_text_append_text(g->backend_combo, "cuda");
  gtk_widget_set_valign(GTK_WIDGET(g->backend_combo), GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(brow, GTK_WIDGET(g->backend_combo));
  adw_preferences_group_add(den, GTK_WIDGET(brow));

  ScaleRow(g, den, "Suppression", "Broadband noise depth (dB)", 0, 48, 1, 0,
           &g->suppression);
  ScaleRow(g, den, "VAD threshold", "Voice gate strictness", 0, 1, 0.05, 2,
           &g->vad);
  ScaleRow(g, den, "Keyboard boost", "Extra key-click cut", 0, 1, 0.05, 2,
           &g->keyboard);

  GtkWidget* save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect_swapped(save, "clicked", G_CALLBACK(SaveFromWidgets), g);
  AdwActionRow* srow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(srow), "Apply");
  adw_action_row_set_subtitle(srow, "Input/backend changes need a restart");
  adw_action_row_add_suffix(srow, save);
  adw_preferences_group_add(den, GTK_WIDGET(srow));

  // ---- Service + meters ----
  AdwPreferencesGroup* svc =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(svc, "Service");
  adw_preferences_page_add(page, svc);

  AdwActionRow* strow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(strow), "Status");
  g->svc_label = GTK_LABEL(gtk_label_new("○ Stopped"));
  gtk_widget_set_valign(GTK_WIDGET(g->svc_label), GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(strow, GTK_WIDGET(g->svc_label));
  adw_preferences_group_add(svc, GTK_WIDGET(strow));

  auto meter = [&](const char* title, GtkLevelBar** out) {
    AdwActionRow* mrow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mrow), title);
    *out = GTK_LEVEL_BAR(gtk_level_bar_new());
    gtk_level_bar_set_min_value(*out, 0);
    gtk_level_bar_set_max_value(*out, 1);
    gtk_widget_set_size_request(GTK_WIDGET(*out), 180, -1);
    gtk_widget_set_valign(GTK_WIDGET(*out), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(mrow, GTK_WIDGET(*out));
    adw_preferences_group_add(svc, GTK_WIDGET(mrow));
  };
  meter("Input level", &g->in_meter);
  meter("Output level", &g->out_meter);

  AdwActionRow* crow = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(crow), "Counters");
  g->counters = GTK_LABEL(gtk_label_new("—"));
  gtk_widget_set_valign(GTK_WIDGET(g->counters), GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(crow, GTK_WIDGET(g->counters));
  adw_preferences_group_add(svc, GTK_WIDGET(crow));

  GtkWidget* start = gtk_button_new_with_label("Start");
  g_signal_connect_swapped(
      start, "clicked", G_CALLBACK(+[](Gui* gg) {
        system("systemctl --user start nv-broadcast.service");
        Toast(gg, SvcActive() ? "Daemon started" : "Start failed — run nvbcastd manually to see why");
      }),
      g);
  GtkWidget* stop = gtk_button_new_with_label("Stop");
  g_signal_connect_swapped(
      stop, "clicked", G_CALLBACK(+[](Gui* gg) {
        system("systemctl --user stop nv-broadcast.service");
        Toast(gg, "Daemon stopped");
      }),
      g);
  GtkWidget* restart = gtk_button_new_with_label("Restart");
  g_signal_connect_swapped(
      restart, "clicked", G_CALLBACK(+[](Gui* gg) {
        system("systemctl --user restart nv-broadcast.service");
        Toast(gg, "Daemon restarted");
      }),
      g);
  GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(btns), start);
  gtk_box_append(GTK_BOX(btns), stop);
  gtk_box_append(GTK_BOX(btns), restart);
  AdwActionRow* ctl = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctl), "Daemon");
  adw_action_row_add_suffix(ctl, btns);
  adw_preferences_group_add(svc, GTK_WIDGET(ctl));

  g->login_switch = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(
      g->login_switch, SvcEnabled() ? TRUE : FALSE);
  g_signal_connect(
      g->login_switch, "state-set",
      G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer d) -> gboolean {
        Gui* gg = static_cast<Gui*>(d);
        system(state ? "systemctl --user enable nv-broadcast.service"
                     : "systemctl --user disable nv-broadcast.service");
        Toast(gg, state ? "Starts on login" : "Won't start on login");
        return FALSE;  // let the switch animate to the requested state
      }),
      g);
  gtk_widget_set_valign(GTK_WIDGET(g->login_switch), GTK_ALIGN_CENTER);
  AdwActionRow* login = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(login), "Start on login");
  adw_action_row_add_suffix(login, GTK_WIDGET(g->login_switch));
  adw_preferences_group_add(svc, GTK_WIDGET(login));

  LoadToWidgets(g);
  g_timeout_add(200, PollStatus, g);
  gtk_window_present(GTK_WINDOW(g->window));
}

}  // namespace

int main(int argc, char** argv) {
  auto g = std::make_unique<Gui>();
  g->cfg_path = nvb::AppConfig::default_path();
  std::string err;
  if (!g->cfg.load(g->cfg_path, &err)) {
    fprintf(stderr, "bad config %s: %s\n", g->cfg_path.c_str(), err.c_str());
    return 2;
  }
  GtkApplication* app =
      gtk_application_new("net.kehrs.nvbroadcast", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(OnActivate), g.get());
  int rc = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return rc;
}
