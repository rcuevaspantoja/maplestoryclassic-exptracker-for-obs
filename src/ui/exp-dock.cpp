#include "exp-dock.hpp"

#include "exp-filter.hpp"
#include "format.hpp"
#include "meter.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
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

		auto *grid = new QGridLayout();
		int row = 0;
		exp_per_min_ = addStat(grid, row++, "EXPMeter.Dock.ExpPerMin", true);
		exp_per_min_avg_ = addStat(grid, row++, "EXPMeter.Dock.ExpPerMinAvg");
		session_exp_ = addStat(grid, row++, "EXPMeter.Dock.SessionExp", true);
		session_time_ = addStat(grid, row++, "EXPMeter.Dock.SessionTime");
		level_ = addStat(grid, row++, "EXPMeter.Dock.Level");
		next_level_ = addStat(grid, row++, "EXPMeter.Dock.NextLevel", true);
		grid->setColumnStretch(1, 1);
		layout->addLayout(grid);

		auto *buttons = new QHBoxLayout();
		pause_ = new QPushButton(text("EXPMeter.Dock.Pause"), this);
		reset_ = new QPushButton(text("EXPMeter.Dock.Reset"), this);
		buttons->addWidget(pause_);
		buttons->addWidget(reset_);
		layout->addLayout(buttons);
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

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(kRefreshMs);

		sources_->reload(attachedSourceName());
		refresh();
	}

private:
	QLabel *addStat(QGridLayout *grid, int row, const char *key, bool emphasize = false)
	{
		grid->addWidget(new QLabel(text(key), this), row, 0);
		auto *value = new QLabel(QStringLiteral("-"), this);
		value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		if (emphasize) {
			QFont font = value->font();
			font.setBold(true);
			value->setFont(font);
		}
		grid->addWidget(value, row, 1);
		return value;
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
		const expmeter::SessionStats &s = snap.stats;
		updateStatus(snap, attached);

		const QString measuring = text("EXPMeter.Dock.Measuring");
		exp_per_min_->setText(s.rate_ready ? qstr(expmeter::formatNumber(std::llround(s.exp_per_min)))
						   : measuring);
		exp_per_min_avg_->setText(
			s.rate_ready ? qstr(expmeter::formatNumber(std::llround(s.session_exp_per_min))) : measuring);
		session_exp_->setText(s.has_reading ? qstr(expmeter::formatNumber(s.session_exp))
						    : QStringLiteral("-"));
		session_time_->setText(qstr(expmeter::formatDuration(s.active_ms / 1000.0)));
		if (s.has_reading) {
			const QString level = s.level > 0 ? QString::number(s.level) : QStringLiteral("?");
			level_->setText(QStringLiteral("%1 · %2%").arg(level).arg(s.percent_x100 / 100.0, 0, 'f', 2));
		} else {
			level_->setText(QStringLiteral("-"));
		}
		next_level_->setText(s.eta_seconds ? qstr(expmeter::formatDuration(*s.eta_seconds))
						   : (s.has_reading ? measuring : QStringLiteral("-")));

		pause_->setText(text(s.running ? "EXPMeter.Dock.Pause" : "EXPMeter.Dock.Resume"));
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
	QLabel *exp_per_min_;
	QLabel *exp_per_min_avg_;
	QLabel *session_exp_;
	QLabel *session_time_;
	QLabel *level_;
	QLabel *next_level_;
	QPushButton *pause_;
	QPushButton *reset_;
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
