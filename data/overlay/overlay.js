// Polls the plugin for the live stats and fills the overlay.
(function () {
	'use strict';

	const POLL_MS = 1000;

	const LABELS = {
		en: {
			expPerMin: 'EXP/min (last 5 min)',
			expPerMinAvg: 'EXP/min (average)',
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
			expPerMinAvg: 'EXP/min (promedio)',
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
	document.body.classList.add('theme-' + (params.get('theme') || 'default'));
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

	function number(value) {
		return Math.round(value).toLocaleString('en-US');
	}

	function duration(seconds) {
		const total = Math.max(0, Math.round(seconds));
		const h = Math.floor(total / 3600);
		const m = Math.floor((total % 3600) / 60);
		const s = total % 60;
		const mm = String(m).padStart(2, '0');
		const ss = String(s).padStart(2, '0');
		return h > 0 ? `${h}:${mm}:${ss}` : `${m}:${ss}`;
	}

	function setValue(id, text) {
		const el = document.getElementById(id);
		if (el.textContent !== text)
			el.textContent = text;
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
		setValue('exp-per-min', ready ? number(data.exp_per_min) : measuring);
		setValue('exp-per-min-avg', ready ? number(data.exp_per_min_avg) : measuring);
		setValue('exp-per-hour', ready ? number(data.exp_per_hour) : measuring);
		setValue('session-exp', data.has_reading ? number(data.session_exp) : '-');
		setValue('eta', data.eta_seconds !== null ? duration(data.eta_seconds) : data.has_reading ? measuring : '-');

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
