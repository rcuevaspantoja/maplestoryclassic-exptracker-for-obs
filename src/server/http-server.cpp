#include "http-server.hpp"

#include "meter.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include <httplib.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

constexpr const char *kHost = "127.0.0.1";

int g_port = 0;
std::unique_ptr<httplib::Server> g_server;
std::thread g_thread;

std::string jsonNumber(double value)
{
	char out[32];
	std::snprintf(out, sizeof(out), "%.2f", value);
	return out;
}

std::string jsonOptional(const std::optional<double> &value)
{
	return value ? jsonNumber(*value) : "null";
}

const char *statusKey(const expmeter::MeterSnapshot &snap)
{
	if (!snap.has_frame || snap.last_frame_age_ms > 3000)
		return "no-video";
	if (!snap.stats.running)
		return "paused";
	return expmeter::statusName(snap.status);
}

std::string stateJson()
{
	const expmeter::MeterSnapshot snap = expmeter::Meter::instance().snapshot();
	const expmeter::SessionStats &s = snap.stats;
	const char *locale = obs_get_locale();
	std::string json = "{";
	json += "\"status\":\"" + std::string(statusKey(snap)) + "\",";
	json += "\"locale\":\"" + std::string(locale ? locale : "en-US") + "\",";
	json += "\"has_reading\":" + std::string(s.has_reading ? "true" : "false") + ",";
	json += "\"running\":" + std::string(s.running ? "true" : "false") + ",";
	json += "\"hud_visible\":" + std::string(s.hud_visible ? "true" : "false") + ",";
	json += "\"level\":" + std::to_string(s.level) + ",";
	json += "\"exp\":" + std::to_string(s.exp) + ",";
	json += "\"percent\":" + jsonNumber(s.percent_x100 / 100.0) + ",";
	json += "\"session_seconds\":" + std::to_string(s.active_ms / 1000) + ",";
	json += "\"session_exp\":" + std::to_string(s.session_exp) + ",";
	json += "\"rate_ready\":" + std::string(s.rate_ready ? "true" : "false") + ",";
	json += "\"exp_per_min\":" + jsonNumber(s.exp_per_min) + ",";
	json += "\"exp_per_hour\":" + jsonNumber(s.exp_per_hour) + ",";
	json += "\"required_exp\":" + jsonOptional(s.required_exp) + ",";
	json += "\"eta_seconds\":" + jsonOptional(s.eta_seconds);
	json += "}";
	return json;
}

} // namespace

std::string overlayUrl()
{
	return "http://" + std::string(kHost) + ":" + std::to_string(g_port) + "/";
}

bool overlayServerRunning()
{
	return g_server != nullptr;
}

bool startOverlayServer(int port)
{
	stopOverlayServer();
	g_port = port;
	g_server = std::make_unique<httplib::Server>();

	char *dir = obs_module_file("overlay");
	if (!dir || !g_server->set_mount_point("/", dir))
		obs_log(LOG_WARNING, "overlay files not found (%s)", dir ? dir : "data/overlay");
	bfree(dir);

	g_server->Get("/api/state", [](const httplib::Request &, httplib::Response &res) {
		res.set_header("Cache-Control", "no-store");
		res.set_content(stateJson(), "application/json");
	});

	// httplib's default SO_REUSEADDR lets two programs share a port on Windows without any
	// error, so a conflict would go unnoticed. Claim the port exclusively instead.
	g_server->set_socket_options([](socket_t sock) {
#ifdef _WIN32
		int yes = 1;
		setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&yes), sizeof(yes));
#else
		int yes = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
	});

	if (!g_server->bind_to_port(kHost, port)) {
		obs_log(LOG_WARNING, "could not start the overlay server: port %d is in use", port);
		g_server.reset();
		return false;
	}
	obs_log(LOG_INFO, "overlay available at %s", overlayUrl().c_str());
	httplib::Server *server = g_server.get();
	g_thread = std::thread([server] { server->listen_after_bind(); });
	return true;
}

void stopOverlayServer()
{
	if (!g_server)
		return;
	g_server->stop();
	if (g_thread.joinable())
		g_thread.join();
	g_server.reset();
}
