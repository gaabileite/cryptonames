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

        // The ready button needs a seat
        const readyButton = $('ready-btn');
        readyButton.disabled = !you.team;
        readyButton.textContent = you.ready ? 'Cancel ready' : "I'm ready";
        readyButton.setAttribute('aria-pressed', you.ready ? 'true' : 'false');
    }

    // One action at a time in the waiting room as well, so quick double clicks do not collide.
    let waitBusy = false;
    async function waitAction(request) {
        if (waitBusy) return;
        waitBusy = true;
        showError('wait-error', '');
        try {
            await request();
        } catch (error) {
            if (error.status === 401) throw error;
            showError('wait-error', error.message);
        } finally {
            waitBusy = false;
            refreshPlay().catch(() => {});
        }
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
        button.disabled = !(state.yourTurn && card.state === 'SECRET');
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

    function renderGame(state, board) {
        show('game');
        document.title = state.yourTurn ? `Your turn - ${BASE_TITLE}` : BASE_TITLE;
        if (!changed('game', { state, board })) return;

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
        } else if (isSpy) {
            hint = 'You are the spy: you see every colour. Give your guesser clues outside the app.';
        } else if (state.yourTurn) {
            hint = 'Your turn: pick a card, or end your turn.';
        } else {
            hint = `Waiting for team ${NAMES[state.currentTeam]} to play.`;
        }
        $('hint').textContent = hint;

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
