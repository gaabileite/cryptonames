// Front-end of the board game. Talks to the JSON API of the server (see src/server/server.cpp).
// Three screens: join (name + seat), wait (for the other players) and game (board + end-turn button).
// The page asks the server for the state every POLL_MS; everything the player sees comes from the server.
(() => {
    'use strict';

    const POLL_MS = 1500;
    const BASE_TITLE = document.title;
    const SEATS = [['BLUE', 'SPY'], ['BLUE', 'GUESSER'], ['RED', 'SPY'], ['RED', 'GUESSER']];
    const NAMES = { BLUE: 'Blue', RED: 'Red', SPY: 'Spy', GUESSER: 'Guesser', BEIGE: 'beige', BLACK: 'black' };

    const $ = (id) => document.getElementById(id);
    const screens = { join: $('screen-join'), wait: $('screen-wait'), game: $('screen-game') };

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
            item.append(chip(player.team), name, ` ${NAMES[player.role]}`);
            list.append(item);
        }
    }

    function renderWhoAmI(me) {
        const element = $('whoami');
        element.replaceChildren(chip(me.team), `${me.name} · ${NAMES[me.role]}`);
        element.hidden = false;
    }

    function forgetSeat() {
        token = null;
        storage.set(null);
        $('whoami').hidden = true;
        lastRendered = {};
        document.title = BASE_TITLE;
    }

    // Skips a render when the data is the same as last time, so the page does not flicker.
    let lastRendered = {};
    function changed(screen, data) {
        const serialized = JSON.stringify(data);
        if (lastRendered[screen] === serialized) return false;
        lastRendered[screen] = serialized;
        return true;
    }

    // ---------- Join screen ----------

    const seatInputs = {};
    (function buildSeats() {
        for (const [team, role] of SEATS) {
            const label = document.createElement('label');
            label.className = `seat team-${team}`;
            const input = document.createElement('input');
            input.type = 'radio';
            input.name = 'seat';
            input.value = `${team}|${role}`;
            const box = document.createElement('span');
            const title = document.createElement('span');
            title.className = 'strong';
            title.textContent = `${NAMES[team]} ${NAMES[role]}`;
            const sub = document.createElement('span');
            sub.className = 'sub';
            box.append(title, sub);
            label.append(input, box);
            $('seats').append(label);
            seatInputs[input.value] = { input, sub };
        }
    })();

    function showJoinError(message) {
        $('join-error').textContent = message || '';
        $('join-error').hidden = !message;
    }

    function renderJoin(lobby) {
        show('join');
        if (!changed('join', lobby)) return;

        const open = {};
        for (const slot of lobby.openSlots) {
            const key = `${slot.team}|${slot.role}`;
            open[key] = (open[key] || 0) + 1;
        }
        for (const key of Object.keys(seatInputs)) {
            const { input, sub } = seatInputs[key];
            input.disabled = lobby.started || !open[key];
            if (input.disabled) input.checked = false;
            sub.textContent = open[key] ? '' : 'taken';
        }
        $('join-btn').disabled = lobby.started;
        $('name').disabled = lobby.started;
        $('join-note').hidden = !lobby.started;
        $('join-note').textContent = lobby.started ? 'A game is already in progress. Ask whoever runs the server to start a new one.' : '';
        renderPlayers($('join-players'), lobby.players);
    }

    $('join-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        const name = $('name').value.trim();
        const seat = document.querySelector('input[name="seat"]:checked');
        if (!name) return showJoinError('Type your name.');
        if (!seat) return showJoinError('Pick a team and a role.');

        const [team, role] = seat.value.split('|');
        showJoinError('');
        $('join-btn').disabled = true;
        try {
            const joined = await api('POST', '/api/join', { name, team, role });
            token = joined.token;
            storage.set(token);
            lastRendered = {};
            await refreshPlay();
        } catch (error) {
            showJoinError(error.message);
            lastRendered = {};
            refreshJoin().catch(() => {});
        } finally {
            $('join-btn').disabled = false;
        }
    });

    async function refreshJoin() {
        renderJoin(await api('GET', '/api/lobby'));
    }

    // ---------- Wait screen ----------

    function renderWait(lobby) {
        show('wait');
        if (!changed('wait', lobby)) return;
        const total = lobby.players.length + lobby.openSlots.length;
        $('wait-count').textContent = `${lobby.players.length} of ${total} players have joined. The game starts when every seat is taken.`;
        renderPlayers($('wait-players'), lobby.players);
    }

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
            button.append(image, tag);
            $('board').append(button);
            cardButtons.push(button);
        }
    })();

    function describeCard(card, isSpy) {
        if (card.state === 'REVEALED') return `${card.name}, revealed: ${NAMES[card.key]}`;
        return isSpy ? `${card.name}, secret: ${NAMES[card.key]}` : card.name;
    }

    function renderGame(state, board) {
        show('game');
        document.title = state.yourTurn ? `Your turn - ${BASE_TITLE}` : BASE_TITLE;
        if (!changed('game', { state, board })) return;

        const me = state.you;
        const isSpy = me.role === 'SPY';
        renderWhoAmI(me);

        // Banner
        const banner = $('banner');
        if (state.finished) {
            banner.className = `banner team-${state.winner}`;
            banner.textContent = `Team ${NAMES[state.winner]} wins!`;
        } else {
            banner.className = `banner team-${state.currentTeam}`;
            banner.textContent = `Team ${NAMES[state.currentTeam]}'s turn`;
        }
        $('score-BLUE').textContent = `Blue: ${state.remaining.BLUE} left`;
        $('score-RED').textContent = `Red: ${state.remaining.RED} left`;

        // Hint
        let hint;
        if (state.finished) {
            hint = state.winner === me.team ? 'Your team won.' : 'Your team lost.';
        } else if (isSpy) {
            hint = 'You are the spy: you see every colour. Give your guesser clues outside the app.';
        } else if (state.yourTurn) {
            hint = 'Your turn: pick a card, or end your turn.';
        } else {
            hint = `Waiting for team ${NAMES[state.currentTeam]} to play.`;
        }
        $('hint').textContent = hint;

        // Board
        for (const card of board.cards) {
            const button = cardButtons[card.index];
            const image = button.querySelector('img');
            const source = `/${card.image}`;
            if (image.getAttribute('src') !== source) image.setAttribute('src', source);
            image.alt = card.name;

            button.className = `card key-${card.key}${card.state === 'REVEALED' ? ' revealed' : ''}${isSpy ? ' spy-view' : ''}`;
            button.disabled = !(state.yourTurn && card.state === 'SECRET');
            button.setAttribute('aria-label', describeCard(card, isSpy));

            // The colour as text too, so it does not depend on seeing the colours.
            const tag = button.querySelector('.tag');
            if (card.state === 'REVEALED') tag.textContent = `${NAMES[card.key]} ✓`;
            else tag.textContent = isSpy ? NAMES[card.key] : '';
        }

        // End-turn button: only guessers have it, and only on their team's turn
        $('end-turn').hidden = isSpy || state.finished;
        $('end-turn').disabled = !state.yourTurn;
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

    // ---------- Refresh loop ----------

    async function refreshPlay() {
        const state = await api('GET', '/api/state');
        myTeam = state.you.team;
        if (!state.started) {
            renderWhoAmI(state.you);
            renderWait(await api('GET', '/api/lobby'));
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
