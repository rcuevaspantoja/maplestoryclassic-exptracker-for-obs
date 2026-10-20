// Polls the plugin for the live stats and fills the overlay.
(function () {
	'use strict';

	const POLL_MS = 1000;
	// How often each value is allowed to change, so the numbers don't flicker on every poll.
	const RATE_REFRESH_MS = 60000;
	const SESSION_REFRESH_MS = 10000;

	const LABELS = {
		en: {
			expPerMin: 'EXP/min (last 5 min)',
			expPerHour: 'EXP/hour',
			sessionExp: 'Session EXP',
			eta: 'Next level in',
			measuring: 'measuring…',
			paused: 'Paused',
			hidden: 'HUD not visible',
			noVideo: 'Waiting for the game',
		},
		es: {
			expPerMin: 'EXP/min (últimos 5 min)',
			expPerHour: 'EXP/hora',
			sessionExp: 'EXP de la sesión',
			eta: 'Siguiente nivel en',
			measuring: 'midiendo…',
			paused: 'En pausa',
			hidden: 'HUD no visible',
			noVideo: 'Esperando el juego',
		},
	};

	const params = new URLSearchParams(window.location.search);
	const root = document.getElementById('exp-meter');
	document.body.classList.add('theme-' + (params.get('theme') || 'solid'));
	for (const id of (params.get('hide') || '').split(',')) {
		const stat = id && document.getElementById('stat-' + id.trim());
		if (stat)
			stat.hidden = true;
	}

	let labels = null;

	function pickLabels(locale) {
		const lang = (params.get('lang') || locale || 'en').slice(0, 2).toLowerCase();
		return LABELS[lang] || LABELS.en;
	}

	function applyLabels() {
		for (const el of document.querySelectorAll('[data-label]'))
			el.textContent = labels[el.dataset.label];
	}

	const SUFFIXES = [
		[1e9, 'B'],
		[1e6, 'M'],
		[1e3, 'K'],
	];

	// 999 -> "999", 1000 -> "1K", 1399 -> "1.3K", 2500000 -> "2.5M".
	// Truncates to one decimal so the value doesn't flicker on every small gain.
	function number(value) {
		const abs = Math.abs(Math.round(value));
		const sign = value < 0 && abs > 0 ? '-' : '';
		for (const [size, suffix] of SUFFIXES) {
			if (abs >= size)
				return sign + Math.floor((abs / size) * 10) / 10 + suffix;
		}
		return sign + abs;
	}

	// 4350 -> "1h 13m", 75 -> "2m". Rounds up so it never shows "0m" before the level up.
	function duration(seconds) {
		const total = Math.max(1, Math.ceil(seconds / 60));
		const h = Math.floor(total / 60);
		const m = total % 60;
		return h > 0 ? `${h}h ${String(m).padStart(2, '0')}m` : `${m}m`;
	}

	function setValue(id, text) {
		const el = document.getElementById(id);
		if (el.textContent !== text)
			el.textContent = text;
	}

	const lastUpdate = {};

	// Updates the value at most once every `interval` ms, but right away when it
	// switches between a number and a placeholder ("measuring…", "-").
	function setThrottled(id, interval, ready, text) {
		const now = Date.now();
		const last = lastUpdate[id];
		if (last && last.ready === ready && now - last.at < interval)
			return;
		lastUpdate[id] = { at: now, ready };
		setValue(id, text);
	}

	function setState(state, message) {
		root.className = 'state-' + state;
		setValue('status', message || '');
	}

	function render(data) {
		if (!labels) {
			labels = pickLabels(data.locale);
			applyLabels();
		}
		const ready = data.rate_ready;
		const measuring = labels.measuring;
		setThrottled('exp-per-min', RATE_REFRESH_MS, ready, ready ? number(data.exp_per_min) : measuring);
		setThrottled('exp-per-hour', RATE_REFRESH_MS, ready, ready ? number(data.exp_per_hour) : measuring);
		setThrottled('session-exp', SESSION_REFRESH_MS, data.has_reading,
			     data.has_reading ? number(data.session_exp) : '-');
		const hasEta = data.eta_seconds !== null;
		setThrottled('eta', SESSION_REFRESH_MS, hasEta,
			     hasEta ? duration(data.eta_seconds) : data.has_reading ? measuring : '-');

		if (data.status === 'no-video')
			setState('no-video', labels.noVideo);
		else if (data.status === 'paused')
			setState('paused', labels.paused);
		else if (!data.hud_visible)
			setState('hidden', labels.hidden);
		else
			setState('ok', '');
	}

	async function poll() {
		try {
			const res = await fetch('/api/state', { cache: 'no-store' });
			render(await res.json());
		} catch (e) {
			if (!labels) {
				labels = pickLabels(navigator.language);
				applyLabels();
			}
			setState('offline', labels.noVideo);
		}
		setTimeout(poll, POLL_MS);
	}

	poll();
})();
