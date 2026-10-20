#include "exp-dock.hpp"

#include "exp-filter.hpp"
#include "meter.hpp"
#include "server/http-server.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <vector>

namespace {

constexpr const char *kDockId = "exp_meter_dock";
constexpr int kRefreshMs = 500;
constexpr int64_t kNoFramesMs = 3000;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString qstr(const std::string &s)
{
	return QString::fromStdString(s);
}

bool hasExpFilter(obs_source_t *source)
{
	bool found = false;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (std::string(obs_source_get_id(filter)) == kExpFilterId)
				*static_cast<bool *>(param) = true;
		},
		&found);
	return found;
}

std::vector<std::string> videoSourceNames()
{
	std::vector<std::string> names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)
				static_cast<std::vector<std::string> *>(param)->push_back(obs_source_get_name(source));
			return true;
		},
		&names);
	return names;
}

// The source that currently has the EXP Meter filter, if any.
std::string attachedSourceName()
{
	std::string name;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (hasExpFilter(source)) {
				*static_cast<std::string *>(param) = obs_source_get_name(source);
				return false;
			}
			return true;
		},
		&name);
	return name;
}

void removeExpFilter(obs_source_t *source)
{
	std::vector<obs_source_t *> filters;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (std::string(obs_source_get_id(filter)) == kExpFilterId)
				static_cast<std::vector<obs_source_t *> *>(param)->push_back(filter);
		},
		&filters);
	for (obs_source_t *filter : filters)
		obs_source_filter_remove(source, filter);
}

// Moves the filter to `name` (or removes it everywhere when `name` is empty), so exactly one
// source feeds the measurement.
void attachExpFilter(const std::string &name)
{
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (obs_source_get_name(source) != *static_cast<const std::string *>(param))
				removeExpFilter(source);
			return true;
		},
		const_cast<std::string *>(&name));
	if (name.empty())
		return;

	obs_source_t *source = obs_get_source_by_name(name.c_str());
	if (!source)
		return;
	if (!hasExpFilter(source)) {
		obs_source_t *filter =
			obs_source_create(kExpFilterId, obs_module_text("EXPMeter.Filter"), nullptr, nullptr);
		obs_source_filter_add(source, filter);
		obs_source_release(filter);
	}
	obs_source_release(source);
}

// Overlay color themes, in the order shown in the dock: theme id and label key.
constexpr const char *kThemes[][2] = {
	{"solid", "EXPMeter.Theme.Solid"}, {"transparent", "EXPMeter.Theme.Transparent"},
	{"maple", "EXPMeter.Theme.Maple"}, {"light", "EXPMeter.Theme.Light"},
	{"ocean", "EXPMeter.Theme.Ocean"},
};

std::string overlayUrlFor(const std::string &theme)
{
	return theme == "solid" ? overlayUrl() : overlayUrl() + "?theme=" + theme;
}

// Points an existing overlay Browser Source at the chosen design.
void applyThemeToOverlay(const std::string &theme)
{
	obs_source_t *overlay = obs_get_source_by_name(obs_module_text("EXPMeter.Overlay.SourceName"));
	if (!overlay)
		return;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "url", overlayUrlFor(theme).c_str());
	obs_source_update(overlay, settings);
	obs_data_release(settings);
	obs_source_release(overlay);
}

// Adds the overlay Browser Source to the current scene (creating it the first time).
bool addOverlayToScene(const std::string &theme)
{
	const char *name = obs_module_text("EXPMeter.Overlay.SourceName");
	obs_source_t *scene_source = obs_frontend_get_current_scene();
	if (!scene_source)
		return false;
	obs_scene_t *scene = obs_scene_from_source(scene_source);

	obs_source_t *overlay = obs_get_source_by_name(name);
	if (!overlay) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", overlayUrlFor(theme).c_str());
		// The panel fills the whole source; 380x150 fits its five rows.
		obs_data_set_int(settings, "width", 380);
		obs_data_set_int(settings, "height", 150);
		overlay = obs_source_create("browser_source", name, settings, nullptr);
		obs_data_release(settings);
	}
	bool ok = overlay != nullptr;
	if (overlay && scene && !obs_scene_find_source(scene, name))
		ok = obs_scene_add(scene, overlay) != nullptr;
	obs_source_release(overlay);
	obs_source_release(scene_source);
	return ok;
}

// Re-lists the sources every time it opens, so new ones show up.
class SourceComboBox : public QComboBox {
public:
	using QComboBox::QComboBox;

	void reload(const std::string &selected)
	{
		blockSignals(true);
		clear();
		addItem(text("EXPMeter.Dock.SelectSource"), QString());
		for (const std::string &name : videoSourceNames())
			addItem(qstr(name), qstr(name));
		const int index = findData(qstr(selected));
		setCurrentIndex(index >= 0 ? index : 0);
		blockSignals(false);
	}

	void showPopup() override
	{
		reload(attachedSourceName());
		QComboBox::showPopup();
	}
};

class ExpDock : public QWidget {
public:
	explicit ExpDock(QWidget *parent) : QWidget(parent)
	{
		auto *layout = new QVBoxLayout(this);

		auto *source_row = new QHBoxLayout();
		source_row->addWidget(new QLabel(text("EXPMeter.Dock.GameSource"), this));
		sources_ = new SourceComboBox(this);
		sources_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		source_row->addWidget(sources_, 1);
		layout->addLayout(source_row);

		status_ = new QLabel(this);
		status_->setWordWrap(true);
		layout->addWidget(status_);

		auto *buttons = new QHBoxLayout();
		pause_ = new QPushButton(text("EXPMeter.Dock.Pause"), this);
		reset_ = new QPushButton(text("EXPMeter.Dock.Reset"), this);
		buttons->addWidget(pause_);
		buttons->addWidget(reset_);
		layout->addLayout(buttons);

		auto *theme_row = new QHBoxLayout();
		theme_row->addWidget(new QLabel(text("EXPMeter.Dock.OverlayTheme"), this));
		themes_ = new QComboBox(this);
		for (const auto &theme : kThemes)
			themes_->addItem(text(theme[1]), QString::fromUtf8(theme[0]));
		const expmeter::Settings settings = expmeter::loadSettings();
		const int saved = themes_->findData(qstr(settings.overlay_theme));
		themes_->setCurrentIndex(saved >= 0 ? saved : 0);
		theme_row->addWidget(themes_, 1);
		add_overlay_ = new QPushButton(text("EXPMeter.Dock.AddOverlay"), this);
		theme_row->addWidget(add_overlay_);
		layout->addLayout(theme_row);

		auto *port_row = new QHBoxLayout();
		port_row->addWidget(new QLabel(text("EXPMeter.Dock.OverlayPort"), this));
		port_ = new QSpinBox(this);
		port_->setRange(1024, 65535);
		port_->setValue(settings.overlay_port);
		port_->setKeyboardTracking(false); // apply when editing finishes, not on every digit
		port_row->addWidget(port_, 1);
		layout->addLayout(port_row);

		url_ = new QLabel(this);
		url_->setWordWrap(true);
		url_->setTextInteractionFlags(Qt::TextBrowserInteraction);
		url_->setOpenExternalLinks(true);
		layout->addWidget(url_);
		updateUrl();
		layout->addStretch(1);

		connect(sources_, &QComboBox::activated, this, [this](int index) {
			attachExpFilter(sources_->itemData(index).toString().toStdString());
			refresh();
		});
		connect(pause_, &QPushButton::clicked, this, [this] {
			if (expmeter::Meter::instance().snapshot().stats.running)
				expmeter::Meter::instance().pause();
			else
				expmeter::Meter::instance().resume();
			refresh();
		});
		connect(reset_, &QPushButton::clicked, this, [this] {
			const auto answer = QMessageBox::question(this, text("EXPMeter.Dock.ResetTitle"),
								  text("EXPMeter.Dock.ResetText"));
			if (answer == QMessageBox::Yes) {
				expmeter::Meter::instance().reset();
				refresh();
			}
		});

		connect(themes_, &QComboBox::activated, this, [this](int index) {
			expmeter::Settings settings = expmeter::loadSettings();
			settings.overlay_theme = themes_->itemData(index).toString().toStdString();
			expmeter::saveSettings(settings);
			applyThemeToOverlay(settings.overlay_theme);
		});
		connect(port_, &QSpinBox::valueChanged, this, [this](int port) {
			expmeter::Settings settings = expmeter::loadSettings();
			if (port == settings.overlay_port && overlayServerRunning())
				return;
			settings.overlay_port = port;
			expmeter::saveSettings(settings);
			if (startOverlayServer(port))
				applyThemeToOverlay(settings.overlay_theme); // point the overlay at the new port
			updateUrl();
		});
		connect(add_overlay_, &QPushButton::clicked, this, [this] {
			if (!addOverlayToScene(themes_->currentData().toString().toStdString()))
				QMessageBox::warning(this, text("EXPMeter.Dock.Title"),
						     text("EXPMeter.Dock.AddOverlayFailed"));
		});

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(kRefreshMs);

		sources_->reload(attachedSourceName());
		refresh();
	}

private:
	// Called on every refresh, so the message always matches the server's real state.
	void updateUrl()
	{
		QString message;
		if (overlayServerRunning()) {
			message = QStringLiteral("%1 <a href=\"%2\">%2</a>")
					  .arg(text("EXPMeter.Dock.OverlayUrl"), qstr(overlayUrl()));
		} else {
			message = QStringLiteral("<span style=\"color:#d03030\">%1</span>")
					  .arg(text("EXPMeter.Dock.PortInUse").arg(port_->value()));
		}
		if (url_->text() != message) // avoid resetting a link the user is hovering
			url_->setText(message);
	}

	void setStatus(const QString &message, const char *color)
	{
		status_->setText(QStringLiteral("<span style=\"color:%1\">&#9679;</span> %2").arg(color, message));
	}

	void refresh()
	{
		const std::string attached = attachedSourceName();
		if (!sources_->view()->isVisible() && sources_->currentData().toString().toStdString() != attached)
			sources_->reload(attached);

		const expmeter::MeterSnapshot snap = expmeter::Meter::instance().snapshot();
		updateStatus(snap, attached);
		updateUrl();

		pause_->setText(text(snap.stats.running ? "EXPMeter.Dock.Pause" : "EXPMeter.Dock.Resume"));
	}

	void updateStatus(const expmeter::MeterSnapshot &snap, const std::string &attached)
	{
		if (attached.empty()) {
			setStatus(text("EXPMeter.Dock.Status.NoSource"), "#9e9e9e");
			return;
		}
		if (!snap.has_frame || snap.last_frame_age_ms > kNoFramesMs) {
			setStatus(text("EXPMeter.Dock.Status.NoFrames"), "#e0a000");
			return;
		}
		if (!snap.stats.running) {
			setStatus(text("EXPMeter.Dock.Status.Paused"), "#e0a000");
			return;
		}
		switch (snap.status) {
		case expmeter::ReadStatus::Ok:
			setStatus(text("EXPMeter.Dock.Status.Reading"), "#3cb043");
			break;
		case expmeter::ReadStatus::HudNotFound:
			setStatus(text("EXPMeter.Dock.Status.HudNotFound"), "#e0a000");
			break;
		case expmeter::ReadStatus::Unreadable:
			setStatus(text("EXPMeter.Dock.Status.Unreadable"), "#e0a000");
			break;
		case expmeter::ReadStatus::UnsupportedScale:
			setStatus(text("EXPMeter.Dock.Status.UnsupportedScale"), "#d03030");
			break;
		}
	}

	SourceComboBox *sources_;
	QLabel *status_;
	QPushButton *pause_;
	QPushButton *reset_;
	QComboBox *themes_;
	QSpinBox *port_;
	QLabel *url_;
	QPushButton *add_overlay_;
};

} // namespace

void registerExpDock()
{
	auto *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id(kDockId, obs_module_text("EXPMeter.Dock.Title"), new ExpDock(main_window));
}

void unregisterExpDock()
{
	obs_frontend_remove_dock(kDockId);
}
