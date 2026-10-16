// Front-end of the board game. Talks to the JSON API of the server (see src/server/server.cpp).
// Three screens: join (just a name), wait (pick a team and a role, then press ready) and game (board + end-turn button).
// The page asks the server for the state every POLL_MS; everything the player sees comes from the server.
(() => {
    'use strict';

    const POLL_MS = 1500;
    const FLIP_MS = 220;      // each half of the card-turning animation
    const BASE_TITLE = document.title;
    const TEAMS = ['BLUE', 'RED'];
    const ROLES = ['SPY', 'GUESSER'];
    const UNIVERSE_NAMES = { 'MCU': 'MCU', 'FOX X-MEN': 'Fox X-Men', 'SONY SPIDER-MAN': 'Sony Spider-Man', 'FANTASTIC FOUR': 'Fantastic Four', 'THROUGH THE SPIDERVERSE': 'Through the Spider-Verse', 'COMICS': 'Comics', 'VENOM': 'Venom' };
    const NAMES = { BLUE: 'Blue', RED: 'Red', SPY: 'Spy', GUESSER: 'Guesser', BEIGE: 'Beige', BLACK: 'Black' };

    const $ = (id) => document.getElementById(id);
    const screens = { join: $('screen-join'), wait: $('screen-wait'), game: $('screen-game') };
    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

    // ---------- Token (kept so a page refresh does not lose the seat) ----------

    const storage = {
        get() { try { return localStorage.getItem('token'); } catch (e) { return null; } },
        set(value) {
            try {
                if (value) localStorage.setItem('token', value); else localStorage.removeItem('token');
            } catch (e) { /* storage blocked: the seat just will not survive a refresh */ }
        },
    };
    let token = storage.get();

    // ---------- API ----------

    async function api(method, path, body) {
        const headers = {};
        if (token) headers['X-Token'] = token;
        if (body !== undefined) headers['Content-Type'] = 'application/json';

        const response = await fetch(path, {
            method,
            headers,
            body: body === undefined ? undefined : JSON.stringify(body),
            cache: 'no-store',
        });
        let data = null;
        try { data = await response.json(); } catch (e) { /* no JSON body */ }
        if (!response.ok) {
            const error = new Error((data && data.error) || `Request failed (${response.status})`);
            error.status = response.status;
            throw error;
        }
        return data;
    }

    // ---------- Small helpers ----------

    function show(name) {
        for (const key of Object.keys(screens)) screens[key].hidden = key !== name;
    }

    function chip(team) {
        const element = document.createElement('span');
        element.className = `chip team-${team}`;
        element.textContent = NAMES[team];
        return element;
    }

    let toastTimer = null;
    function toast(message, isError) {
        const element = $('toast');
        element.textContent = message;
        element.className = isError ? 'toast error' : 'toast';
        element.hidden = false;
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => { element.hidden = true; }, 4000);
    }

    function setConnection(message) {
        const element = $('connection');
        element.textContent = message || '';
        element.hidden = !message;
    }

    function showError(id, message) {
        $(id).textContent = message || '';
        $(id).hidden = !message;
    }

    // Players that have no seat yet appear without a team.
    function renderPlayers(list, players) {
        list.replaceChildren();
        if (players.length === 0) {
            const item = document.createElement('li');
            item.className = 'empty';
            item.textContent = 'Nobody yet.';
            list.append(item);
        }
        for (const player of players) {
            const item = document.createElement('li');
            const name = document.createElement('strong');
            name.textContent = player.name;      // textContent: a name can never inject HTML
            if (player.team) item.append(chip(player.team), name, ` ${NAMES[player.role]}`);
            else item.append(name);
            list.append(item);
        }
    }

    function renderWhoAmI(me) {
        const element = $('whoami');
        if (me.team) element.replaceChildren(chip(me.team), `${me.name} · ${NAMES[me.role]}`);
        else element.replaceChildren(me.name);
        element.hidden = false;
    }

    // Skips a render when the data is the same as last time, so the page does not flicker.
    let lastRendered = {};
    let lastStates = null;          // the state of each card the last time the board was drawn (null: not drawn yet)
    function changed(screen, data) {
        const serialized = JSON.stringify(data);
        if (lastRendered[screen] === serialized) return false;
        lastRendered[screen] = serialized;
        return true;
    }

    function resetRendering() {
        lastRendered = {};
        lastStates = null;
    }

    function forgetSeat() {
        token = null;
        storage.set(null);
        $('whoami').hidden = true;
        resetRendering();
        document.title = BASE_TITLE;
    }

    // ---------- Join screen: just a name ----------

    function renderJoin(lobby) {
        show('join');
        if (!changed('join', lobby)) return;

        const full = lobby.players.length >= lobby.totalSeats;
        const closed = lobby.started || full;
        $('join-btn').disabled = closed;
        $('name').disabled = closed;
        $('join-note').hidden = !closed;
        $('join-note').textContent = lobby.finished
            ? 'The last game is over. The room opens again when all its players have left.'
            : (lobby.started ? 'A game is already in progress. Wait until it is over and its players have left the room.'
                : (full ? 'The waiting room is full.' : ''));
        renderPlayers($('join-players'), lobby.players);
    }

    $('join-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        const name = $('name').value.trim();
        if (!name) return showError('join-error', 'Type your name.');

        showError('join-error', '');
        $('join-btn').disabled = true;
        try {
            const joined = await api('POST', '/api/join', { name });
            token = joined.token;
            storage.set(token);
            resetRendering();
            await refreshPlay();
        } catch (error) {
            showError('join-error', error.message);
            resetRendering();
            refreshJoin().catch(() => {});
        } finally {
            $('join-btn').disabled = false;
        }
    });

    async function refreshJoin() {
        renderJoin(await api('GET', '/api/lobby'));
    }

    // ---------- Waiting room: pick a team and a role, then press ready ----------

    const slots = {};     // 'BLUE|SPY' -> { button, who, badge }
    (function buildTeams() {
        for (const team of TEAMS) {
            const card = document.createElement('div');
            card.className = `team-card team-${team}`;
            const title = document.createElement('h3');
            title.textContent = `${NAMES[team]} team`;
            card.append(title);
            for (const role of ROLES) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'slot';
                button.dataset.team = team;
                button.dataset.role = role;
                const roleName = document.createElement('span');
                roleName.className = 'role';
                roleName.textContent = NAMES[role];
                const who = document.createElement('span');
                who.className = 'who';
                const badge = document.createElement('span');
                badge.className = 'badge';
                button.append(roleName, who, badge);
                card.append(button);
                slots[`${team}|${role}`] = { button, who, badge };
            }
            $('teams').append(card);
        }
    })();

    // Universes: one checkbox per universe that has characters, built the first time the server tells us which they are.
    const universeBoxes = {};     // 'MCU' -> { input, num }
    function buildUniverses(list) {
        for (const universe of list) {
            const label = document.createElement('label');
            label.className = 'uni';
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.value = universe.name;
            const pill = document.createElement('span');
            pill.className = 'pill';
            const check = document.createElement('span');
            check.className = 'check';
            check.setAttribute('aria-hidden', 'true');
            const name = document.createElement('span');
            name.textContent = UNIVERSE_NAMES[universe.name] || universe.name;
            const num = document.createElement('span');
            num.className = 'num';
            pill.append(check, name, num);
            label.append(input, pill);
            $('universe-list').append(label);
            universeBoxes[universe.name] = { input, num };
        }
    }

    function renderUniverses(lobby) {
        if (Object.keys(universeBoxes).length === 0) buildUniverses(lobby.universes);
        for (const universe of lobby.universes) {
            const { input, num } = universeBoxes[universe.name];
            input.checked = universe.selected;
            // the last universes that still make a full board cannot be switched off
            const needed = lobby.charactersNeeded;
            const cannotTurnOff = universe.selected && lobby.charactersSelected - universe.count < needed;
            input.disabled = cannotTurnOff;
            input.title = cannotTurnOff ? `Without these characters there are fewer than ${needed}, and a game needs ${needed}.` : '';
            num.textContent = `${universe.count} characters`;
        }
        $('universe-count').textContent = `${lobby.charactersSelected} characters selected. A game needs at least ${lobby.charactersNeeded}.`;
    }

    // Time limits: one for the spy to give the clue and one for the guesser, each with a checkbox, the seconds and quick choices.
    const TIMER_ROWS = [
        { key: 'clue', checkbox: 'Limit the time to give the clue', label: 'Seconds for the clue', presets: [[30, '30 s'], [60, '1 min'], [120, '2 min']] },
        { key: 'guess', checkbox: 'Limit the time to guess', label: 'Seconds for guessing', presets: [[30, '30 s'], [60, '1 min'], [120, '2 min'], [300, '5 min']] },
    ];
    const timerRows = {};         // 'clue' -> { on, seconds, presets: [buttons] }
    (function buildTimerRows() {
        for (const row of TIMER_ROWS) {
            const wrapper = document.createElement('div');
            wrapper.className = 'timer-row';

            const check = document.createElement('label');
            check.className = 'check-row';
            const on = document.createElement('input');
            on.type = 'checkbox';
            on.id = `timer-${row.key}-on`;
            check.append(on, ` ${row.checkbox}`);

            const line = document.createElement('div');
            line.className = 'timer-seconds';
            const label = document.createElement('label');
            label.htmlFor = `timer-${row.key}-seconds`;
            label.textContent = row.label;
            const seconds = document.createElement('input');
            seconds.type = 'number';
            seconds.id = `timer-${row.key}-seconds`;
            seconds.inputMode = 'numeric';
            seconds.step = '5';
            const presetBox = document.createElement('span');
            presetBox.className = 'presets';
            presetBox.id = `timer-${row.key}-presets`;
            const presets = [];
            for (const [value, text] of row.presets) {
                const button = document.createElement('button');
                button.type = 'button';
                button.textContent = text;
                button.dataset.seconds = value;
                presetBox.append(button);
                presets.push(button);
            }
            line.append(label, seconds, presetBox);
            wrapper.append(check, line);
            $('timer-rows').append(wrapper);
            timerRows[row.key] = { on, seconds, presets };

            on.addEventListener('change', () => {
                waitAction(() => api('POST', '/api/timer', { phase: row.key, enabled: on.checked })).catch(() => {});
            });
            seconds.addEventListener('change', () => {
                const value = Number(seconds.value);
                if (seconds.value.trim() === '' || !Number.isInteger(value) || value < Number(seconds.min) || value > Number(seconds.max)) {
                    showError('wait-error', `The time can be from ${seconds.min} to ${seconds.max} seconds.`);
                    delete lastRendered.wait;          // put the value of the server back
                    seconds.blur();
                    refreshPlay().catch(() => {});
                    return;
                }
                waitAction(() => api('POST', '/api/timer', { phase: row.key, enabled: on.checked, seconds: value })).catch(() => {});
            });
            presetBox.addEventListener('click', (event) => {
                const button = event.target.closest('button');
                if (!button || button.disabled) return;
                waitAction(() => api('POST', '/api/timer', { phase: row.key, enabled: true, seconds: Number(button.dataset.seconds) })).catch(() => {});
            });
        }
    })();

    function formatSeconds(total) {
        const minutes = Math.floor(total / 60), seconds = total % 60;
        return minutes === 0 ? `${seconds} seconds` : (seconds === 0 ? `${minutes} min` : `${minutes} min ${seconds} s`);
    }

    function renderTimerSetup(lobby) {
        const timers = lobby.timers;
        for (const row of TIMER_ROWS) {
            const limit = timers[row.key];
            const { on, seconds, presets } = timerRows[row.key];
            on.checked = limit.enabled;
            seconds.min = timers.min;
            seconds.max = timers.max;
            seconds.disabled = !limit.enabled;
            if (document.activeElement !== seconds) seconds.value = limit.seconds;    // never overwrite what somebody is typing
            for (const button of presets) {
                button.disabled = !limit.enabled;
                button.classList.toggle('on', limit.enabled && Number(button.dataset.seconds) === limit.seconds);
            }
        }
        const clue = timers.clue, guess = timers.guess;
        if (!clue.enabled && !guess.enabled) {
            $('timer-note').textContent = "No time limits: the spy and the guesser can take as long as they need. Anybody can turn a limit on, and doing so cancels everybody's ready.";
        } else {
            $('timer-note').textContent =
                (clue.enabled ? `The spy has at most ${formatSeconds(clue.seconds)} to give the clue. ` : 'The clue has no time limit. ') +
                (guess.enabled ? `The guesser has at most ${formatSeconds(guess.seconds)}, counted from the moment the clue is sent. ` : 'Guessing has no time limit. ') +
                `When time runs out, the turn passes to the other team (from ${timers.min} to ${timers.max} seconds can be chosen). Changing a limit cancels everybody's ready.`;
        }
    }

    let me = null;        // who I am in the waiting room, from the last state

    function renderWait(lobby, you) {
        show('wait');
        me = you;
        if (!changed('wait', { lobby, you })) return;

        const players = lobby.players;
        const total = lobby.totalSeats;
        const ready = players.filter((player) => player.ready).length;
        const unseated = players.filter((player) => !player.team);

        // Who sits where
        const occupant = {};
        for (const player of players) {
            if (player.team) occupant[`${player.team}|${player.role}`] = player;
        }
        for (const key of Object.keys(slots)) {
            const { button, who, badge } = slots[key];
            const player = occupant[key];
            const mine = !!player && player.name === you.name;
            button.classList.toggle('mine', mine);
            button.classList.toggle('taken', !!player && !mine);
            button.setAttribute('aria-pressed', mine ? 'true' : 'false');
            who.textContent = player ? (mine ? `${player.name} (you)` : player.name) : 'Open';
            badge.textContent = player && player.ready ? '✓ Ready' : '';
            // Taken by someone else, or locked because I am ready
            button.disabled = (!!player && !mine) || you.ready;
        }

        // Status line
        let status;
        if (players.length < total) status = `${players.length} of ${total} players have joined. Waiting for ${total - players.length} more.`;
        else if (unseated.length > 0) status = 'Everybody needs to pick a team and a role.';
        else if (ready < total) status = `${ready} of ${total} players are ready. The game starts when everyone is ready.`;
        else status = 'Everyone is ready. Starting...';
        $('wait-count').textContent = status;

        $('wait-unseated').hidden = unseated.length === 0;
        $('wait-unseated').textContent = `Still choosing a team: ${unseated.map((player) => player.name).join(', ')}`;
        $('wait-locked').hidden = !you.ready;

        renderUniverses(lobby);
        renderTimerSetup(lobby);

        // The ready button needs a seat
        const readyButton = $('ready-btn');
        readyButton.disabled = !you.team;
        readyButton.textContent = you.ready ? 'Cancel ready' : "I'm ready";
        readyButton.setAttribute('aria-pressed', you.ready ? 'true' : 'false');
    }

    // The changes of the waiting room (seat, ready, universes, time limits) go to the server one after the other, in the
    // order they were made, so none of them is lost when two are made quickly (for example, two time limits in a row).
    let waitQueue = Promise.resolve();
    function waitAction(request) {
        const run = waitQueue.then(async () => {
            showError('wait-error', '');
            try {
                await request();
            } catch (error) {
                if (error.status === 401) throw error;
                showError('wait-error', error.message);
            } finally {
                delete lastRendered.wait;        // redraw from the server's data, which also puts back anything that was rejected
                refreshPlay().catch(() => {});
            }
        });
        waitQueue = run.catch(() => {});         // a failed change does not stop the ones after it
        return run;
    }

    $('teams').addEventListener('click', (event) => {
        const button = event.target.closest('button.slot');
        if (!button || button.disabled) return;
        waitAction(() => api('POST', '/api/seat', { team: button.dataset.team, role: button.dataset.role })).catch(() => {});
    });

    $('ready-btn').addEventListener('click', () => {
        if (!me) return;
        waitAction(() => api('POST', '/api/ready', { ready: !me.ready })).catch(() => {});
    });

    $('leave-btn').addEventListener('click', async () => {
        try {
            await api('POST', '/api/leave');
        } catch (error) {
            if (error.status !== 401) return showError('wait-error', error.message);
        }
        forgetSeat();
        refreshJoin().catch(() => {});
    });

    // Only the universe that was clicked is sent, so a page that is a moment out of date cannot undo what somebody else just did.
    $('universe-list').addEventListener('change', (event) => {
        const input = event.target;
        waitAction(() => api('POST', '/api/universes', { universe: input.value, selected: input.checked })).catch(() => {});
    });

    // ---------- Game screen ----------

    const cardButtons = [];
    (function buildBoard() {
        for (let i = 0; i < 25; i++) {
            const button = document.createElement('button');
            button.type = 'button';
            button.dataset.index = i;
            const image = document.createElement('img');
            image.alt = '';
            const tag = document.createElement('span');
            tag.className = 'tag';
            const name = document.createElement('span');     // shown on hover, see style.css
            name.className = 'name';
            button.append(image, tag, name);
            $('board').append(button);
            cardButtons.push(button);
        }
    })();

    let latest = null;                // { state, cards } of the last render: what every card should look like now
    const flipping = new Set();       // indexes of the cards that are being turned over

    function describeCard(card, isSpy) {
        if (card.state === 'REVEALED') return `${card.name}, revealed: ${NAMES[card.key]}`;
        return isSpy ? `${card.name}, secret: ${NAMES[card.key]}` : card.name;
    }

    // Draws one card the way the latest state says.
    function paintCard(index) {
        const { state, cards } = latest;
        const card = cards[index];
        const isSpy = state.you.role === 'SPY';
        const button = cardButtons[index];

        const image = button.querySelector('img');
        const source = `/${card.image}`;
        if (image.getAttribute('src') !== source) image.setAttribute('src', source);
        image.alt = card.name;
        button.querySelector('.name').textContent = card.name;

        button.className = `card key-${card.key}${card.state === 'REVEALED' ? ' revealed' : ''}${isSpy ? ' spy-view' : ''}`;
        button.disabled = !(state.canGuess && card.state === 'SECRET');
        button.setAttribute('aria-label', describeCard(card, isSpy));

        // The colour as text too, so it does not depend on seeing the colours.
        const tag = button.querySelector('.tag');
        if (card.state === 'REVEALED') tag.textContent = `${NAMES[card.key]} ✓`;
        else tag.textContent = isSpy ? NAMES[card.key] : '';
    }

    function canAnimate() {
        return typeof Element.prototype.animate === 'function'
            && !document.hidden
            && !window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    }

    // Resolves when the picture is ready (or fails, or takes too long), so the turn never shows an empty card.
    function preload(source) {
        return new Promise((resolve) => {
            const image = new Image();
            image.onload = image.onerror = () => resolve();
            image.src = source;
            setTimeout(resolve, 1000);
        });
    }

    // Turns the card over: it spins edge-on, the other face is drawn, and it spins back.
    async function flipCard(index) {
        flipping.add(index);
        const button = cardButtons[index];
        const spin = (from, to, easing) => button.animate(
            [{ transform: `perspective(900px) rotateY(${from}deg)` }, { transform: `perspective(900px) rotateY(${to}deg)` }],
            { duration: FLIP_MS, easing, fill: 'forwards' });
        try {
            const loading = preload(`/${latest.cards[index].image}`);
            const first = spin(0, 90, 'ease-in');
            await Promise.race([first.finished, sleep(FLIP_MS + 300)]);
            await loading;
            paintCard(index);                        // edge-on, so the change is not seen
            const second = spin(-90, 0, 'ease-out');
            first.cancel();
            await Promise.race([second.finished, sleep(FLIP_MS + 300)]);
            second.cancel();
        } catch (error) {
            // an animation that was cancelled: the final state is drawn below
        } finally {
            flipping.delete(index);
            paintCard(index);
        }
    }

    // The countdown of the turn. The server says how many milliseconds are left, and this counts down from there;
    // the time that matters is always the server's, so this is only what the player sees.
    let turnDeadline = null;          // performance.now() value at which the time of the phase ends
    let timerLabel = '';              // which phase it counts: the clue or the guessing
    let timeUpRefreshFor = null;      // the deadline for which the refresh at 0 was already asked
    function updateTimer(timer, finished) {
        const element = $('turn-timer');
        if (!timer || !timer.enabled || finished || timer.millisecondsLeft === null) {
            turnDeadline = null;
            element.hidden = true;
            return;
        }
        turnDeadline = performance.now() + timer.millisecondsLeft;
        timerLabel = timer.phase === 'CLUE' ? 'Clue' : 'Guessing';
        element.hidden = false;
        paintTimer();
    }

    function paintTimer() {
        if (turnDeadline === null) return;
        const element = $('turn-timer');
        const left = Math.max(0, turnDeadline - performance.now());
        const seconds = Math.ceil(left / 1000);
        element.textContent = left === 0 ? "Time's up!" : `${timerLabel} ⏱ ${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
        element.classList.toggle('low', seconds <= 10);
        if (left === 0 && timeUpRefreshFor !== turnDeadline) {
            timeUpRefreshFor = turnDeadline;            // the server passes the turn by itself: just go and look
            setTimeout(() => refreshPlay().catch(() => {}), 300);
        }
    }
    setInterval(paintTimer, 250);

    // ---------- Clues ----------

    function plural(count, word) {
        if (count === 1) return `1 ${word}`;
        return word.endsWith('y') ? `${count} ${word.slice(0, -1)}ies` : `${count} ${word}s`;      // try -> tries
    }

    function span(className, text) {
        const element = document.createElement('span');
        element.className = className;
        element.textContent = text;           // textContent: a clue can never inject HTML
        return element;
    }

    function renderClues(state) {
        const bar = $('clue-bar'), form = $('clue-form');
        bar.replaceChildren();
        bar.className = 'clue-bar';

        // The clue of the turn, for everybody to see
        if (!state.finished && state.clue) {
            const clue = state.clue;
            bar.className = `clue-bar team-${clue.team}`;
            bar.append('Clue: ', span('word', clue.text), span('num', String(clue.number)));
            bar.append(span('tries', `${plural(clue.triesLeft, 'try')} left · found ${clue.found} of ${clue.number}`));
            if (clue.bonusTries > 0) bar.append(span('sub', `includes ${plural(clue.bonusTries, 'extra try')} from the last turn`));
        } else if (!state.finished && !state.canGiveClue) {
            bar.className = `clue-bar team-${state.currentTeam}`;
            bar.append(state.you.team === state.currentTeam && state.you.role === 'GUESSER'
                ? "Waiting for your spy's clue..."
                : `Team ${NAMES[state.currentTeam]}'s spy is thinking of a clue...`);
        }

        // The form of the spy whose turn it is to give the clue
        const wasHidden = form.hidden;
        form.hidden = !state.canGiveClue;
        if (state.canGiveClue) {
            const select = $('clue-number');
            const cardsLeft = state.remaining[state.you.team];
            if (select.options.length !== cardsLeft) {          // keep what was chosen while the options are the same
                const chosen = Number(select.value) || 1;
                select.replaceChildren();
                for (let n = 1; n <= cardsLeft; n++) select.append(new Option(String(n), String(n)));
                select.value = String(Math.min(chosen, cardsLeft));
            }
            const extra = state.extraTries[state.you.team];
            $('clue-extra').hidden = extra === 0;
            $('clue-extra').textContent = `Your guesser also gets ${plural(extra, 'extra try')} from the last turn, on top of the number you pick.`;
            if (wasHidden) $('clue-text').focus();
        } else {
            $('clue-error').hidden = true;
        }

        // Every clue so far, newest first
        const list = $('clue-list');
        list.replaceChildren();
        for (const clue of state.clues.slice().reverse().slice(0, 12)) {
            const item = document.createElement('li');
            item.append(chip(clue.team), span('word', clue.text), `${clue.number}`,
                span('result', `found ${clue.found} of ${clue.number}${clue.bonusTries > 0 ? ` · +${clue.bonusTries} extra tries` : ''}`));
            list.append(item);
        }
    }

    function showClueError(message) {
        $('clue-error').textContent = message || '';
        $('clue-error').hidden = !message;
    }

    $('clue-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        if (busy) return;
        const words = $('clue-text').value.trim().split(/\s+/).filter(Boolean);
        if (words.length === 0) return showClueError('Type a clue.');
        if (words.length > 2) return showClueError('A clue can have at most 2 words.');
        showClueError('');
        busy = true;
        $('clue-btn').disabled = true;
        try {
            await api('POST', '/api/clue', { text: words.join(' '), number: Number($('clue-number').value) });
            $('clue-text').value = '';
        } catch (error) {
            showClueError(error.message);
        } finally {
            busy = false;
            $('clue-btn').disabled = false;
            refreshPlay().catch(() => {});
        }
    });

    function renderGame(state, board) {
        show('game');
        document.title = state.canGuess || state.canGiveClue ? `Your turn - ${BASE_TITLE}` : BASE_TITLE;
        updateTimer(state.timer, state.finished);
        const { timer, ...stable } = state;       // the time left changes with every poll, which must not redraw the board
        if (!changed('game', { state: stable, board })) return;

        latest = { state, cards: board.cards };
        const you = state.you;
        const isSpy = you.role === 'SPY';
        renderWhoAmI(you);

        // Banner
        const banner = $('banner');
        if (state.finished) {
            banner.className = `banner team-${state.winner}`;
            banner.textContent = state.forfeitedBy
                ? `Team ${NAMES[state.forfeitedBy]} forfeited. Team ${NAMES[state.winner]} wins!`
                : `Team ${NAMES[state.winner]} wins!`;
        } else {
            banner.className = `banner team-${state.currentTeam}`;
            banner.textContent = `Team ${NAMES[state.currentTeam]}'s turn`;
        }
        $('score-BLUE').textContent = `Blue: ${state.remaining.BLUE} left`;
        $('score-RED').textContent = `Red: ${state.remaining.RED} left`;

        // Hint
        let hint;
        if (state.finished && state.forfeitedBy) {
            hint = state.forfeitedBy === you.team ? 'Your team forfeited.' : 'The other team forfeited.';
        } else if (state.finished) {
            hint = state.winner === you.team ? 'Your team won.' : 'Your team lost.';
        } else if (state.canGiveClue) {
            hint = 'Your turn: give your guesser a clue. You see every colour.';
        } else if (isSpy && state.currentTeam === you.team) {
            hint = 'Your clue is out. Your guesser is picking cards.';
        } else if (isSpy) {
            hint = `Waiting for team ${NAMES[state.currentTeam]} to play. You see every colour.`;
        } else if (state.canGuess) {
            hint = `Your turn: pick a card (${state.clue.triesLeft} ${state.clue.triesLeft === 1 ? 'try' : 'tries'} left), or end your turn.`;
        } else if (state.yourTurn) {
            hint = "Waiting for your spy's clue. You can also end the turn.";
        } else {
            hint = `Waiting for team ${NAMES[state.currentTeam]} to play.`;
        }
        $('hint').textContent = hint;

        renderClues(state);

        // Board: a card that was secret and is now revealed is turned over (for every player), the others are just redrawn.
        const turnedOver = lastStates
            ? board.cards.filter((card) => card.state === 'REVEALED' && lastStates[card.index] === 'SECRET').map((card) => card.index)
            : [];
        lastStates = board.cards.map((card) => card.state);
        for (const card of board.cards) {
            if (flipping.has(card.index)) continue;        // its animation draws it with the latest data
            if (turnedOver.includes(card.index) && canAnimate()) flipCard(card.index);
            else paintCard(card.index);
        }

        // End-turn button: only guessers have it, and only on their team's turn
        $('end-turn').hidden = isSpy || state.finished;
        $('end-turn').disabled = !state.yourTurn;
        renderActions(state.finished);
    }

    // Forfeit and leave: any player can give up at any time, or leave the room. A second click confirms.
    // Leaving a match that is still going on makes the team forfeit, so the confirmation says so.
    let asking = null;                // null, 'forfeit' or 'leave'
    let gameFinished = false;
    let askedFrom = null;             // the button that opened the question, to give the focus back to it
    function renderActions(finished) {
        gameFinished = finished;
        if (finished) asking = null;           // nothing to give up any more, and leaving no longer forfeits
        $('forfeit-btn').hidden = finished || asking !== null;
        $('leave-room-btn').hidden = asking !== null;
        $('confirm-box').hidden = asking === null;
    }

    let busy = false;     // one action at a time, so a double click does not guess twice
    let myTeam = null;    // set by refreshPlay(), used to tell a good guess from a bad one

    $('board').addEventListener('click', async (event) => {
        const button = event.target.closest('button.card');
        if (!button || button.disabled || busy) return;
        busy = true;
        try {
            const result = await api('POST', '/api/guess', { index: Number(button.dataset.index) });
            if (result.key === 'BLACK') toast('That was the black card. Your team loses.', true);
            else if (result.key === myTeam) toast(`Correct! That was a ${NAMES[result.key]} card.`);
            else toast(`That was a ${NAMES[result.key]} card. The turn passes.`, true);
        } catch (error) {
            toast(error.message, true);
        } finally {
            busy = false;
            refreshPlay().catch(() => {});
        }
    });

    $('end-turn').addEventListener('click', async () => {
        if (busy) return;
        busy = true;
        try {
            await api('POST', '/api/end-turn');
            toast('Turn passed to the other team.');
        } catch (error) {
            toast(error.message, true);
        } finally {
            busy = false;
            refreshPlay().catch(() => {});
        }
    });

    function ask(mode, from) {
        asking = mode;
        askedFrom = from;
        if (mode === 'forfeit') {
            const other = myTeam === 'BLUE' ? 'Red' : 'Blue';
            $('confirm-question').textContent = `Give up and let Team ${other} win?`;
            $('confirm-yes').textContent = 'Yes, forfeit';
        } else if (mode === 'leave') {
            $('confirm-question').textContent = 'Leaving the room makes your team forfeit. Leave anyway?';
            $('confirm-yes').textContent = 'Yes, leave';
        }
        renderActions(gameFinished);
        // keyboard focus goes to the safe option, so a stray Enter does not forfeit or leave
        $('confirm-no').focus();
    }

    function stopAsking() {
        asking = null;
        renderActions(gameFinished);
        if (askedFrom) askedFrom.focus();
    }

    async function leaveTheRoom() {
        if (busy) return;
        busy = true;
        try {
            await api('POST', '/api/leave');
        } catch (error) {
            if (error.status !== 401) {          // 401: the server no longer knows us, which is the same as having left
                busy = false;
                asking = null;
                renderActions(gameFinished);
                return toast(error.message, true);
            }
        }
        busy = false;
        asking = null;
        forgetSeat();
        refreshJoin().catch(() => {});
    }

    async function forfeit() {
        if (busy) return;
        busy = true;
        try {
            await api('POST', '/api/forfeit');
            toast('Your team forfeited.', true);
        } catch (error) {
            toast(error.message, true);
        } finally {
            busy = false;
            asking = null;
            renderActions(gameFinished);
            refreshPlay().catch(() => {});
        }
    }

    $('forfeit-btn').addEventListener('click', () => ask('forfeit', $('forfeit-btn')));
    // After the match there is nothing to forfeit, so leaving needs no question
    $('leave-room-btn').addEventListener('click', () => (gameFinished ? leaveTheRoom() : ask('leave', $('leave-room-btn'))));
    $('confirm-no').addEventListener('click', stopAsking);
    $('confirm-yes').addEventListener('click', () => (asking === 'leave' ? leaveTheRoom() : forfeit()));
    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && asking) stopAsking();
    });

    // ---------- Refresh loop ----------

    async function refreshPlay() {
        const state = await api('GET', '/api/state');
        myTeam = state.you.team;
        if (!state.started) {
            renderWhoAmI(state.you);
            renderWait(await api('GET', '/api/lobby'), state.you);
            return;
        }
        renderGame(state, await api('GET', '/api/board'));
    }

    async function tick() {
        try {
            if (token) await refreshPlay(); else await refreshJoin();
            setConnection('');
        } catch (error) {
            if (error.status === 401) {
                forgetSeat();            // the server forgot this token (it was restarted): back to the join screen
                refreshJoin().catch(() => {});
            } else {
                setConnection('Connection to the server lost. Retrying...');
            }
        }
        setTimeout(tick, POLL_MS);
    }

    tick();
})();
