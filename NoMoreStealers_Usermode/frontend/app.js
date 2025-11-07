/**
 * NoMoreStealers frontend controller.
 * Encapsulates websocket connectivity, event rendering and the Antispy feature.
 */
class NoMoreStealers {
    constructor() {
        this.events = [];
        this.eventCount = 0;
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.antispyActive = false;
        this.pollInterval = 1000;
        this.tutorialStep = 1;
        this.maxTutorialSteps = 6;
        this.currentFilter = 'all'; // 'all', 'antispy', 'antirat'
        this.eventDetailsOverlay = null;
        this.eventDetailsBody = null;
        this.eventDetailsTitle = null;
        this.eventDetailsClose = null;
        this.eventDetailsInitialized = false;
        this.settings = {
            virusTotalApiKey: ''
        };
        this.settingsStatusTimer = null;
        window.addEventListener('DOMContentLoaded', () => this.init());
        setInterval(() => this.updateAntispyStatus(), this.pollInterval);
    }

    /**
     * Initialize the UI and start websocket connection.
     */
    init() {
        this.connectWebSocket();
        this.initAntispyButton();
        this.initTabs();
        this.initSettingsTab();
        this.initTutorial();
        this.initClearButton();
        this.initKillswitchButton();
        this.initEventFilters();
        this.initEventDetailsModal();
        this.updateAntispyStatus();
        this.updateKillswitchStatus();
        setInterval(() => this.updateKillswitchStatus(), this.pollInterval);
    }

    /**
     * Establish a websocket connection to the backend and handle reconnects.
     */
    connectWebSocket() {
        try {
            this.ws = new WebSocket('ws://localhost:34116/ws');

            this.ws.onopen = () => {
                this.reconnectAttempts = 0;
            };

            this.ws.onmessage = (event) => {
                try {
                    const eventData = JSON.parse(event.data);
                    if (Array.isArray(eventData)) {
                        eventData.forEach(ev => this.addEvent(ev));
                    } else {
                        this.addEvent(eventData);
                    }
                } catch (e) {
                    console.error('Parse error:', e);
                }
            };

            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
            };

            this.ws.onclose = () => {
                if (this.reconnectAttempts < this.maxReconnectAttempts) {
                    this.reconnectAttempts++;
                    setTimeout(() => this.connectWebSocket(), 1000 * this.reconnectAttempts);
                } else {
                    setInterval(() => this.fetchEvents(), 500);
                }
            };
        } catch (e) {
            setInterval(() => this.fetchEvents(), 500);
        }
    }

    /**
     * Poll the backend for recent events when websocket is unavailable.
     */
    async fetchEvents() {
        try {
            if (window.go?.app?.App?.GetEvents) {
                const newEvents = await window.go.app.App.GetEvents();
                if (newEvents?.length) newEvents.forEach(event => this.addEvent(event));
            } else if (window.GetEvents) {
                const newEvents = await window.GetEvents();
                if (newEvents?.length) newEvents.forEach(event => this.addEvent(event));
            }
        } catch (error) {
            console.error('Fetch error:', error);
        }
    }

    /**
     * Add an event to the UI list.
     * @param {object} event
     */
    addEvent(event) {
        this.events.unshift(event);
        this.eventCount++;
        const countEl = document.getElementById('eventCount');
        if (countEl) {
            countEl.textContent = this.eventCount;
            countEl.classList.add('count-highlight');
            setTimeout(() => countEl.classList.remove('count-highlight'), 450);
        }
        const emptyState = document.getElementById('emptyState');
        if (emptyState) emptyState.style.display = 'none';
        
        const tutorial = document.getElementById('driverTutorial');
        
        const isDriverError = event.type === 'error' && 
            (event.path?.includes('driver') || event.path?.includes('Failed to initialize kernel communication') || event.processName === 'System');
        
        if (isDriverError && tutorial) {
            if (tutorial.classList.contains('hidden')) {
                this.tutorialStep = 1;
                this.updateTutorialStep();
            }
            tutorial.classList.remove('hidden');
        } else if (event.type !== 'error' && tutorial) {
            tutorial.classList.add('hidden');
        }
        
        // Only add to DOM if it matches current filter
        if (this.matchesFilter(event, this.currentFilter)) {
            const eventsList = document.getElementById('eventsList');
            const eventCard = this.createEventCard(event);
            if (eventsList) eventsList.insertBefore(eventCard, eventsList.firstChild);
            requestAnimationFrame(() => eventCard.classList.add('fade-in'));
        }
        
        if (this.events.length > 100) {
            this.events.pop();
            const eventsList = document.getElementById('eventsList');
            if (eventsList?.lastChild) eventsList.removeChild(eventsList.lastChild);
        }
    }
    
    /**
     * Check if an event matches the current filter
     */
    matchesFilter(event, filter) {
        if (filter === 'all') return true;
        
        if (filter === 'antirat') {
            return event.type === 'killswitch_terminated' || event.type === 'killswitch_failed';
        }
        
        if (filter === 'antispy') {
            // Normal logs: blocked, error, and other non-killswitch events
            return event.type !== 'killswitch_terminated' && 
                   event.type !== 'killswitch_failed';
        }
        
        return true;
    }
    
    /**
     * Render all events based on current filter
     */
    renderFilteredEvents() {
        const eventsList = document.getElementById('eventsList');
        if (!eventsList) return;
        
        eventsList.innerHTML = '';
        
        const filteredEvents = this.events.filter(event => this.matchesFilter(event, this.currentFilter));
        
        if (filteredEvents.length === 0) {
            // Show empty state if no events match filter
            const emptyState = document.getElementById('emptyState');
            if (emptyState) {
                emptyState.style.display = 'flex';
            }
            return;
        }
        
        // Hide empty state
        const emptyState = document.getElementById('emptyState');
        if (emptyState) {
            emptyState.style.display = 'none';
        }
        
        filteredEvents.forEach(event => {
            const eventCard = this.createEventCard(event);
            eventsList.appendChild(eventCard);
            requestAnimationFrame(() => eventCard.classList.add('fade-in'));
        });
    }

    /**
     * Create a DOM card for an event.
     * @param {object} event
     * @returns {HTMLElement}
     */
    createEventCard(event) {
        const card = document.createElement('div');
        card.className = 'event-card rounded-2xl p-6 gradient-border';

        // Handle other event types
        if (false) { // Placeholder - no clipboard events anymore
            const statusClass = 'status-blocked';
            const statusIcon = 'fa-clipboard';
            const statusText = 'CLIPBOARD ACCESSED';
            const statusColor = 'text-orange-400';
            const time = new Date(event.timestamp).toLocaleTimeString();

            card.innerHTML = `
            <div class="flex items-start justify-between mb-5">
                <div class="flex items-center space-x-4">
                    <div class="${statusClass} px-5 py-2.5 rounded-xl font-bold text-sm flex items-center space-x-2.5">
                        <i class="fas ${statusIcon} ${statusColor}"></i>
                        <span>${statusText}</span>
                    </div>
                </div>
                <div class="text-gray-400 text-sm font-medium flex items-center space-x-2 bg-gray-800/40 px-3 py-1.5 rounded-lg">
                    <i class="far fa-clock"></i>
                    <span>${time}</span>
                </div>
            </div>
            <div class="space-y-4">
                <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-orange-500/20 to-amber-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-cube text-orange-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Process Name</div>
                        <div class="font-bold text-lg text-white truncate">${this.escapeHtml(event.processName)}</div>
                    </div>
                    <div class="flex items-center space-x-2 bg-orange-500/10 px-3 py-1.5 rounded-lg border border-orange-500/20">
                        <i class="fas fa-hashtag text-orange-400 text-xs"></i>
                        <span class="font-mono font-semibold text-orange-300">PID: ${event.pid || 'N/A'}</span>
                    </div>
                </div>
                ${event.executablePath ? `
                <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-amber-500/20 to-orange-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-file-code text-amber-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Executable Path</div>
                        <div class="font-mono text-sm text-gray-300 break-all leading-relaxed">${this.escapeHtml(event.executablePath)}</div>
                    </div>
                </div>
                ` : ''}
                <div class="flex items-start space-x-4 p-4 bg-orange-900/20 rounded-xl border border-orange-500/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-orange-500/20 to-amber-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-clipboard text-orange-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-orange-400 font-semibold mb-1 uppercase tracking-wider">Clipboard Content Preview</div>
                        <div class="font-mono text-sm text-orange-200 break-all leading-relaxed">${this.escapeHtml(event.path || 'N/A')}</div>
                    </div>
                </div>
            </div>
            `;
            this.decorateEventCard(card, event);
            return card;
        }

        // Handle killswitch events
        if (event.type === 'killswitch_terminated' || event.type === 'killswitch_failed') {
            const isTerminated = event.type === 'killswitch_terminated';
            const statusClass = isTerminated ? 'status-blocked' : 'status-blocked';
            const statusIcon = isTerminated ? 'fa-skull' : 'fa-exclamation-triangle';
            const statusText = isTerminated ? 'KILLSWITCH TERMINATED' : 'KILLSWITCH FAILED';
            const statusColor = isTerminated ? 'text-red-400' : 'text-orange-400';
            const time = new Date(event.timestamp).toLocaleTimeString();

            card.innerHTML = `
            <div class="flex items-start justify-between mb-5">
                <div class="flex items-center space-x-4">
                    <div class="${statusClass} px-5 py-2.5 rounded-xl font-bold text-sm flex items-center space-x-2.5">
                        <i class="fas ${statusIcon} ${statusColor}"></i>
                        <span>${statusText}</span>
                    </div>
                </div>
                <div class="text-gray-400 text-sm font-medium flex items-center space-x-2 bg-gray-800/40 px-3 py-1.5 rounded-lg">
                    <i class="far fa-clock"></i>
                    <span>${time}</span>
                </div>
            </div>
            <div class="space-y-4">
                <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-red-500/20 to-orange-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-cube text-red-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Process Name</div>
                        <div class="font-bold text-lg text-white truncate">${this.escapeHtml(event.processName)}</div>
                    </div>
                    <div class="flex items-center space-x-2 bg-red-500/10 px-3 py-1.5 rounded-lg border border-red-500/20">
                        <i class="fas fa-hashtag text-red-400 text-xs"></i>
                        <span class="font-mono font-semibold text-red-300">PID: ${event.pid || 'N/A'}</span>
                    </div>
                </div>
                ${event.executablePath ? `
                <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-amber-500/20 to-orange-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-file-code text-amber-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Executable Path</div>
                        <div class="font-mono text-sm text-gray-300 break-all leading-relaxed">${this.escapeHtml(event.executablePath)}</div>
                    </div>
                </div>
                ` : ''}
                <div class="flex items-start space-x-4 p-4 bg-red-900/20 rounded-xl border border-red-500/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-red-500/20 to-red-600/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-network-wired text-red-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-red-400 font-semibold mb-1 uppercase tracking-wider">TCP Connections</div>
                        <div class="font-mono text-sm text-red-200 break-all leading-relaxed">${this.escapeHtml(event.path || 'N/A')}</div>
                    </div>
                </div>
            </div>
            `;
            this.decorateEventCard(card, event);
            return card;
        }

        if (event.type === 'error') {
            const isDriverError = event.path?.includes('driver') || 
                                event.path?.includes('Failed to initialize kernel communication') || 
                                event.processName === 'System';
            
            if (isDriverError) {
                card.style.display = 'none';
                return card;
            }
            
            const time = new Date(event.timestamp).toLocaleTimeString();
            card.innerHTML = `
            <div class="flex items-start justify-between mb-5">
                <div class="flex items-center space-x-4">
                    <div class="status-blocked px-5 py-2.5 rounded-xl font-bold text-sm flex items-center space-x-2.5">
                        <i class="fas fa-exclamation-triangle text-red-400"></i>
                        <span>ERROR</span>
                    </div>
                </div>
                <div class="text-gray-400 text-sm font-medium flex items-center space-x-2 bg-gray-800/40 px-3 py-1.5 rounded-lg">
                    <i class="far fa-clock"></i>
                    <span>${time}</span>
                </div>
            </div>
            <div class="space-y-4">
                <div class="flex items-start space-x-4 p-4 bg-red-900/20 rounded-xl border border-red-500/30">
                    <div class="w-10 h-10 bg-gradient-to-br from-red-500/20 to-red-600/20 rounded-lg flex items-center justify-center flex-shrink-0">
                        <i class="fas fa-exclamation-circle text-red-400"></i>
                    </div>
                    <div class="flex-1 min-w-0">
                        <div class="text-xs text-red-400 font-semibold mb-1 uppercase tracking-wider">Error Message</div>
                        <div class="font-mono text-sm text-red-200 whitespace-pre-line break-words leading-relaxed">${this.escapeHtml(event.path || event.processName || 'Unknown error')}</div>
                    </div>
                </div>
            </div>
            `;
            return card;
        }

        const isBlocked = event.type === 'blocked';
        const statusClass = isBlocked ? 'status-blocked' : 'status-allowed';
        const statusIcon = isBlocked ? 'fa-ban' : 'fa-check-circle';
        const statusText = isBlocked ? 'BLOCKED' : 'ALLOWED';
        const statusColor = isBlocked ? 'text-red-400' : 'text-green-400';
        const signIcon = event.isSigned ? 'fa-certificate' : 'fa-exclamation-triangle';
        const signColor = event.isSigned ? 'text-green-400' : 'text-amber-400';
        const signText = event.isSigned ? 'Verified' : 'Unsigned';
        const time = new Date(event.timestamp).toLocaleTimeString();

        card.innerHTML = `
        <div class="flex items-start justify-between mb-5">
            <div class="flex items-center space-x-4">
                <div class="${statusClass} px-5 py-2.5 rounded-xl font-bold text-sm flex items-center space-x-2.5">
                    <i class="fas ${statusIcon} ${statusColor}"></i>
                    <span>${statusText}</span>
                </div>
                <div class="flex items-center space-x-2 px-4 py-2 bg-gray-800/40 rounded-lg border border-gray-700/50">
                    <i class="fas ${signIcon} ${signColor} text-xs"></i>
                    <span class="text-sm font-semibold ${signColor}">${signText}</span>
                </div>
            </div>
            <div class="text-gray-400 text-sm font-medium flex items-center space-x-2 bg-gray-800/40 px-3 py-1.5 rounded-lg">
                <i class="far fa-clock"></i>
                <span>${time}</span>
            </div>
        </div>
        <div class="space-y-4">
            <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                <div class="w-10 h-10 bg-gradient-to-br from-blue-500/20 to-purple-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                    <i class="fas fa-cube text-blue-400"></i>
                </div>
                <div class="flex-1 min-w-0">
                    <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Process Name</div>
                    <div class="font-bold text-lg text-white truncate">${this.escapeHtml(event.processName)}</div>
                </div>
                <div class="flex items-center space-x-2 bg-purple-500/10 px-3 py-1.5 rounded-lg border border-purple-500/20">
                    <i class="fas fa-hashtag text-purple-400 text-xs"></i>
                    <span class="font-mono font-semibold text-purple-300">PID: ${event.pid || 'N/A'}</span>
                </div>
            </div>
            ${event.executablePath ? `
            <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                <div class="w-10 h-10 bg-gradient-to-br from-amber-500/20 to-orange-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                    <i class="fas fa-file-code text-amber-400"></i>
                </div>
                <div class="flex-1 min-w-0">
                    <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Executable Path</div>
                    <div class="font-mono text-sm text-gray-300 break-all leading-relaxed">${this.escapeHtml(event.executablePath)}</div>
                </div>
            </div>
            ` : `
            <div class="flex items-start space-x-4 p-4 bg-red-900/20 rounded-xl border border-red-500/30">
                <div class="w-10 h-10 bg-gradient-to-br from-red-500/20 to-red-600/20 rounded-lg flex items-center justify-center flex-shrink-0">
                    <i class="fas fa-exclamation-triangle text-red-400"></i>
                </div>
                <div class="flex-1 min-w-0">
                    <div class="text-xs text-red-400 font-semibold mb-1 uppercase tracking-wider">Warning</div>
                    <div class="text-sm text-red-200">Could not retrieve executable path (Access Denied)</div>
                </div>
            </div>
            `}
            <div class="flex items-start space-x-4 p-4 bg-gray-800/30 rounded-xl border border-gray-700/30">
                <div class="w-10 h-10 bg-gradient-to-br from-green-500/20 to-emerald-500/20 rounded-lg flex items-center justify-center flex-shrink-0">
                    <i class="fas fa-folder-open text-green-400"></i>
                </div>
                <div class="flex-1 min-w-0">
                    <div class="text-xs text-gray-500 font-semibold mb-1 uppercase tracking-wider">Target Path</div>
                    <div class="font-mono text-sm text-gray-300 break-all leading-relaxed">${this.escapeHtml(event.path || 'N/A')}</div>
                </div>
            </div>
        </div>
        `;
        this.decorateEventCard(card, event);
        return card;
    }

    decorateEventCard(card, event) {
        if (!card || (card.style && card.style.display === 'none')) {
            return;
        }
        card.classList.add('cursor-pointer', 'transition', 'duration-200', 'hover:-translate-y-1', 'hover:border-purple-500/40', 'hover:shadow-lg');
        card.addEventListener('click', (evt) => {
            evt.preventDefault();
            this.openEventDetails(event);
        });
    }

    /**
     * HTML-escape a string.
     * @param {string} text
     * @returns {string}
     */
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    /**
     * Wire the Antispy button to its handler.
     */
    initAntispyButton() {
        const btn = document.getElementById('antispyBtn');
        if (btn) {
            btn.addEventListener('click', () => this.toggleAntispy());
            if (this.antispyActive) btn.classList.add('active'); else btn.classList.remove('active');
        }
    }

    /**
     * Initialize tab buttons for Events / Anti Rat / About / Credits / Settings.
     */
    initTabs() {
        const tabBtns = document.querySelectorAll('.tab-btn');
        const tabs = {
            events: document.getElementById('eventsTab'),
            antirat: document.getElementById('antiratTab'),
            about: document.getElementById('aboutTab'),
            credits: document.getElementById('creditsTab'),
            settings: document.getElementById('settingsTab')
        };

        tabBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                const tab = btn.dataset.tab;
                tabBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');

                Object.values(tabs).forEach(panel => {
                    if (panel) {
                        panel.classList.add('hidden');
                    }
                });

                if (tab && tabs[tab]) {
                    tabs[tab].classList.remove('hidden');
                }

                if (tab === 'settings') {
                    this.loadSettings();
                }
            });
        });
    }

    /**
     * Initialize the settings UI.
     */
    initSettingsTab() {
        const saveBtn = document.getElementById('saveSettingsBtn');
        const form = document.getElementById('settingsForm');

        if (form) {
            form.addEventListener('submit', (evt) => {
                evt.preventDefault();
                const input = document.getElementById('vtApiKeyInput');
                if (input) {
                    this.saveSettings(input.value, saveBtn);
                }
            });
        }

        if (saveBtn) {
            saveBtn.addEventListener('click', (evt) => {
                evt.preventDefault();
                const input = document.getElementById('vtApiKeyInput');
                if (input) {
                    this.saveSettings(input.value, saveBtn);
                }
            });
        }

        this.loadSettings();
    }

    async loadSettings() {
        const input = document.getElementById('vtApiKeyInput');
        if (!input) {
            return;
        }

        try {
            let settings = null;
            if (window.go?.app?.App?.GetSettings) {
                settings = await window.go.app.App.GetSettings();
            } else if (window.GetSettings) {
                settings = await window.GetSettings();
            }

            const key = settings?.virusTotalApiKey || '';
            this.settings.virusTotalApiKey = key;
            input.value = key;
            this.showSettingsStatus('', 'idle');
        } catch (error) {
            console.error('Load settings error:', error);
            this.showSettingsStatus('Failed to load settings: ' + (error?.message || error), 'error');
        }
    }

    async saveSettings(value, button) {
        const trimmed = (value || '').trim();
        const statusButton = button;
        const originalLabel = statusButton ? statusButton.innerHTML : '';
        const input = document.getElementById('vtApiKeyInput');

        try {
            if (statusButton) {
                statusButton.disabled = true;
                statusButton.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Saving…';
            }

            let response = null;
            if (window.go?.app?.App?.UpdateSettings) {
                response = await window.go.app.App.UpdateSettings({ virusTotalApiKey: trimmed });
            } else if (window.UpdateSettings) {
                response = await window.UpdateSettings({ virusTotalApiKey: trimmed });
            } else {
                throw new Error('UpdateSettings binding not available in this build.');
            }

            const storedKey = response?.virusTotalApiKey ?? trimmed;
            this.settings.virusTotalApiKey = storedKey;
            if (input) {
                input.value = storedKey;
            }

            this.showSettingsStatus('Settings saved.', 'success');
        } catch (error) {
            console.error('Save settings error:', error);
            this.showSettingsStatus('Failed to save settings: ' + (error?.message || error), 'error');
        } finally {
            if (statusButton) {
                statusButton.disabled = false;
                statusButton.innerHTML = originalLabel || '<i class="fas fa-save"></i> Save Settings';
            }
        }
    }

    showSettingsStatus(message, state = 'info') {
        const status = document.getElementById('settingsStatus');
        if (!status) {
            return;
        }

        if (this.settingsStatusTimer) {
            clearTimeout(this.settingsStatusTimer);
            this.settingsStatusTimer = null;
        }

        status.classList.remove('hidden', 'text-green-300', 'text-red-300', 'text-gray-400');

        if (state === 'success') {
            status.textContent = message || 'Settings saved.';
            status.classList.add('text-green-300');
            this.settingsStatusTimer = setTimeout(() => {
                status.classList.add('hidden');
            }, 4000);
        } else if (state === 'error') {
            status.textContent = message || 'Something went wrong.';
            status.classList.add('text-red-300');
        } else if (state === 'idle') {
            status.textContent = '';
            status.classList.add('hidden');
        } else {
            status.textContent = message || '';
            status.classList.add('text-gray-400');
        }
    }

    /**
     * Toggle the Antispy overlay via backend binding or global fallback.
     */
    async toggleAntispy() {
        try {
            if (window.go?.app?.App) {
                if (this.antispyActive) {
                    await window.go.app.App.DisableAntispy();
                    this.antispyActive = false;
                } else {
                    await window.go.app.App.EnableAntispy();
                    this.antispyActive = true;
                }
            } else if (window.DisableAntispy && window.EnableAntispy) {
                if (this.antispyActive) {
                    await window.DisableAntispy();
                    this.antispyActive = false;
                } else {
                    await window.ExecuteEnableAntispy ? await window.ExecuteEnableAntispy() : await window.EnableAntispy();
                    this.antispyActive = true;
                }
            } else {
                alert('App methods not available. Please reload the application.');
                return;
            }
            await new Promise(resolve => setTimeout(resolve, 100));
            this.updateAntispyStatus();
        } catch (error) {
            console.error('Antispy toggle error:', error);
            alert('Failed to toggle antispy: ' + (error?.message || error));
        }
    }

    /**
     * Query the backend for the current Antispy status and update the UI.
     */
    async updateAntispyStatus() {
        try {
            if (window.go?.app?.App?.IsAntispyActive) {
                this.antispyActive = await window.go.app.App.IsAntispyActive();
            } else if (window.IsAntispyActive) {
                this.antispyActive = await window.IsAntispyActive();
            }

            const btn = document.getElementById('antispyBtn');
            const text = document.getElementById('antispyText');
            const icon = btn?.querySelector('i');

            if (this.antispyActive) {
                if (text) text.textContent = 'Disable Antispy';
                if (icon) {
                    icon.className = 'fas fa-eye text-red-400 text-lg relative z-10 group-hover:text-red-300 transition-colors';
                }
                if (btn) {
                    btn.classList.remove('border-purple-500/30', 'hover:border-purple-400/60', 'hover:from-purple-600/20', 'hover:to-pink-600/20');
                    btn.classList.add('border-red-500/40', 'hover:border-red-400/70', 'hover:from-red-600/20', 'hover:to-orange-600/20');
                    const overlay = btn.querySelector('.absolute');
                    if (overlay) {
                        overlay.className = 'absolute inset-0 bg-gradient-to-r from-red-500/10 to-orange-500/10 opacity-0 group-hover:opacity-100 transition-opacity duration-300';
                    }
                }
            } else {
                if (text) text.textContent = 'Enable Antispy';
                if (icon) {
                    icon.className = 'fas fa-eye-slash text-purple-400 text-lg relative z-10 group-hover:text-purple-300 transition-colors';
                }
                if (btn) {
                    btn.classList.remove('border-red-500/40', 'hover:border-red-400/70', 'hover:from-red-600/20', 'hover:to-orange-600/20');
                    btn.classList.add('border-purple-500/30', 'hover:border-purple-400/60', 'hover:from-purple-600/20', 'hover:to-pink-600/20');
                    const overlay = btn.querySelector('.absolute');
                    if (overlay) {
                        overlay.className = 'absolute inset-0 bg-gradient-to-r from-purple-500/10 to-pink-500/10 opacity-0 group-hover:opacity-100 transition-opacity duration-300';
                    }
                }
            }
        } catch (error) {
            console.error('Antispy status update error:', error);
        }
    }

    /**
     * Initialize the tutorial step navigation.
     */
    initTutorial() {
        const nextBtn = document.getElementById('tutorialNextBtn');
        const backBtn = document.getElementById('tutorialBackBtn');
        
        if (nextBtn) {
            nextBtn.addEventListener('click', () => this.nextTutorialStep());
        }
        
        if (backBtn) {
            backBtn.addEventListener('click', () => this.prevTutorialStep());
        }
    }

    /**
     * Navigate to the next tutorial step with animation.
     */
    nextTutorialStep() {
        if (this.tutorialStep >= this.maxTutorialSteps) return;
        
        const currentStepEl = document.getElementById(`tutorialStep${this.tutorialStep}`);
        const nextStepEl = document.getElementById(`tutorialStep${this.tutorialStep + 1}`);
        
        if (currentStepEl && nextStepEl) {
            currentStepEl.classList.add('hiding-right');
            setTimeout(() => {
                currentStepEl.classList.add('hidden');
                currentStepEl.classList.remove('hiding-right', 'tutorial-step', 'slide-in-left');
                this.tutorialStep++;
                nextStepEl.classList.remove('hidden');
                nextStepEl.classList.add('tutorial-step');
                this.updateTutorialStep();
            }, 400);
        }
    }

    /**
     * Navigate to the previous tutorial step with animation.
     */
    prevTutorialStep() {
        if (this.tutorialStep <= 1) return;
        
        const currentStepEl = document.getElementById(`tutorialStep${this.tutorialStep}`);
        const prevStepEl = document.getElementById(`tutorialStep${this.tutorialStep - 1}`);
        
        if (currentStepEl && prevStepEl) {
            currentStepEl.classList.add('hiding');
            setTimeout(() => {
                currentStepEl.classList.add('hidden');
                currentStepEl.classList.remove('hiding', 'tutorial-step', 'slide-in-left');
                this.tutorialStep--;
                prevStepEl.classList.remove('hidden');
                prevStepEl.classList.add('tutorial-step', 'slide-in-left');
                this.updateTutorialStep();
            }, 400);
        }
    }

    /**
     * Update the tutorial UI to reflect the current step.
     */
    updateTutorialStep() {
        const stepEl = document.getElementById(`tutorialStep${this.tutorialStep}`);
        if (stepEl) {
            stepEl.classList.remove('hidden');
        }

        const progressBar = document.getElementById('stepProgress');
        const currentStepNum = document.getElementById('currentStepNum');
        const nextBtn = document.getElementById('tutorialNextBtn');
        const backBtn = document.getElementById('tutorialBackBtn');

        if (progressBar) {
            const progress = (this.tutorialStep / this.maxTutorialSteps) * 100;
            progressBar.style.width = `${progress}%`;
        }

        if (currentStepNum) {
            currentStepNum.textContent = this.tutorialStep;
        }

        if (nextBtn) {
            if (this.tutorialStep >= this.maxTutorialSteps) {
                nextBtn.innerHTML = '<span>Complete</span><i class="fas fa-check"></i>';
                nextBtn.disabled = true;
            } else {
                nextBtn.innerHTML = '<span>Next Step</span><i class="fas fa-arrow-right"></i>';
                nextBtn.disabled = false;
            }
        }

        if (backBtn) {
            backBtn.disabled = this.tutorialStep <= 1;
        }

        document.querySelectorAll('.step-indicator').forEach((indicator, index) => {
            const stepNum = index + 1;
            indicator.classList.remove('active', 'completed', 'pending');
            
            if (stepNum < this.tutorialStep) {
                indicator.classList.add('completed');
            } else if (stepNum === this.tutorialStep) {
                indicator.classList.add('active');
            } else {
                indicator.classList.add('pending');
            }
        });
    }

    /**
     * Initialize the Clear All Entries button.
     */
    initClearButton() {
        const btn = document.getElementById('clearAllBtn');
        if (btn) {
            btn.addEventListener('click', () => this.clearAllEvents());
        }
    }

    /**
     * Clear all events from the UI and backend.
     */
    async clearAllEvents() {
        try {
            // Clear backend events
            if (window.go?.app?.App?.ClearAllEvents) {
                await window.go.app.App.ClearAllEvents();
            } else if (window.ClearAllEvents) {
                await window.ClearAllEvents();
            }

            // Clear frontend events
            this.events = [];
            this.eventCount = 0;
            
            // Update UI
            const countEl = document.getElementById('eventCount');
            if (countEl) {
                countEl.textContent = '0';
            }

            this.renderFilteredEvents();

            const emptyState = document.getElementById('emptyState');
            if (emptyState) {
                emptyState.style.display = 'flex';
            }
        } catch (error) {
            console.error('Clear all events error:', error);
            alert('Failed to clear events: ' + (error?.message || error));
        }
    }

    /**
     * Initialize the Killswitch button.
     */
    initKillswitchButton() {
        const btn = document.getElementById('killswitchBtn');
        if (btn) {
            btn.addEventListener('click', () => this.toggleKillswitch());
        }
    }

    /**
     * Toggle the Killswitch via backend binding.
     */
    async toggleKillswitch() {
        try {
            if (window.go?.app?.App) {
                const isActive = await window.go.app.App.IsKillswitchActive();
                if (isActive) {
                    await window.go.app.App.DisableKillswitch();
                } else {
                    await window.go.app.App.EnableKillswitch();
                }
            } else if (window.DisableKillswitch && window.EnableKillswitch) {
                const isActive = await window.IsKillswitchActive();
                if (isActive) {
                    await window.DisableKillswitch();
                } else {
                    await window.EnableKillswitch();
                }
            } else {
                alert('App methods not available. Please reload the application.');
                return;
            }
            await new Promise(resolve => setTimeout(resolve, 100));
            this.updateKillswitchStatus();
        } catch (error) {
            console.error('Killswitch toggle error:', error);
            alert('Failed to toggle killswitch: ' + (error?.message || error));
        }
    }

    /**
     * Query the backend for the current Killswitch status and update the UI.
     */
    async updateKillswitchStatus() {
        try {
            let isActive = false;
            if (window.go?.app?.App?.IsKillswitchActive) {
                isActive = await window.go.app.App.IsKillswitchActive();
            } else if (window.IsKillswitchActive) {
                isActive = await window.IsKillswitchActive();
            }

            const btn = document.getElementById('killswitchBtn');
            const text = document.getElementById('killswitchText');
            const icon = document.getElementById('killswitchIcon');
            const status = document.getElementById('killswitchStatus');
            const statusText = document.getElementById('killswitchStatusText');
            const statusDot = document.getElementById('killswitchStatusDot');

            if (isActive) {
                if (text) text.textContent = 'Disable Killswitch';
                if (icon) {
                    icon.className = 'fas fa-power-off text-3xl relative z-10 group-hover:text-white transition-colors text-red-400';
                }
                if (btn) {
                    btn.classList.remove('hover:from-purple-600/20', 'hover:to-pink-600/20');
                    btn.classList.add('hover:from-red-600/20', 'hover:to-orange-600/20');
                    const overlay = btn.querySelector('.absolute');
                    if (overlay) {
                        overlay.className = 'absolute inset-0 bg-gradient-to-r from-red-500/20 to-orange-500/20 opacity-0 group-hover:opacity-100 transition-opacity duration-300';
                    }
                }
                if (status) status.classList.remove('hidden');
                if (statusText) statusText.textContent = 'Killswitch Active - Monitoring';
                if (statusDot) {
                    statusDot.classList.remove('bg-green-400');
                    statusDot.classList.add('bg-red-400');
                }
            } else {
                if (text) text.textContent = 'Enable Killswitch';
                if (icon) {
                    icon.className = 'fas fa-power-off text-3xl relative z-10 group-hover:text-white transition-colors';
                }
                if (btn) {
                    btn.classList.remove('hover:from-red-600/20', 'hover:to-orange-600/20');
                    btn.classList.add('hover:from-purple-600/20', 'hover:to-pink-600/20');
                    const overlay = btn.querySelector('.absolute');
                    if (overlay) {
                        overlay.className = 'absolute inset-0 bg-gradient-to-r from-red-500/20 to-orange-500/20 opacity-0 group-hover:opacity-100 transition-opacity duration-300';
                    }
                }
                if (status) status.classList.add('hidden');
            }
        } catch (error) {
            console.error('Killswitch status update error:', error);
        }
    }

    /**
     * Initialize event filter buttons
     */
    initEventFilters() {
        const filterBtns = document.querySelectorAll('.filter-btn');
        filterBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                const filter = btn.dataset.filter;
                
                // Update active state
                filterBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                
                // Update filter and re-render
                this.currentFilter = filter;
                this.renderFilteredEvents();
            });
        });
    }

    initEventDetailsModal() {
        if (this.eventDetailsInitialized) {
            return;
        }

        this.eventDetailsOverlay = document.getElementById('eventDetailsOverlay');
        this.eventDetailsBody = document.getElementById('eventDetailsBody');
        this.eventDetailsTitle = document.getElementById('eventDetailsTitle');
        this.eventDetailsClose = document.getElementById('eventDetailsClose');

        if (!this.eventDetailsOverlay || !this.eventDetailsBody) {
            return;
        }

        this.eventDetailsInitialized = true;

        this.eventDetailsOverlay.addEventListener('click', (evt) => {
            if (evt.target === this.eventDetailsOverlay) {
                this.closeEventDetails();
            }
        });

        if (this.eventDetailsClose) {
            this.eventDetailsClose.addEventListener('click', (evt) => {
                evt.preventDefault();
                this.closeEventDetails();
            });
        }

        document.addEventListener('keydown', (evt) => {
            if (evt.key === 'Escape' && this.eventDetailsOverlay && !this.eventDetailsOverlay.classList.contains('hidden')) {
                this.closeEventDetails();
            }
        });
    }

    async openEventDetails(event) {
        if (!this.eventDetailsOverlay || !this.eventDetailsBody) {
            console.warn('Event details modal not available');
            return;
        }

        this.eventDetailsOverlay.classList.remove('hidden');
        document.body.classList.add('overflow-hidden');

        if (this.eventDetailsTitle) {
            this.eventDetailsTitle.textContent = event?.processName ? `${event.processName} — Event Details` : 'Event Details';
        }

        this.eventDetailsBody.innerHTML = this.renderEventDetailsLoading();

        try {
            let details = null;
            if (window.go?.app?.App?.GetEventDetails) {
                details = await window.go.app.App.GetEventDetails(event);
            } else if (window.GetEventDetails) {
                details = await window.GetEventDetails(event);
            }
            this.renderEventDetails(details, event);
        } catch (error) {
            console.error('Event details error:', error);
            const message = error?.message || error?.toString() || 'Failed to load event details.';
            this.eventDetailsBody.innerHTML = `
                <div class="py-12 text-center space-y-3">
                    <i class="fas fa-triangle-exclamation text-3xl text-red-400"></i>
                    <p class="text-gray-200 font-semibold">Unable to load event details</p>
                    <p class="text-sm text-gray-500">${this.escapeHtml(message)}</p>
                </div>
            `;
        }
    }

    closeEventDetails() {
        if (!this.eventDetailsOverlay) {
            return;
        }
        this.eventDetailsOverlay.classList.add('hidden');
        document.body.classList.remove('overflow-hidden');
        if (this.eventDetailsBody) {
            this.eventDetailsBody.innerHTML = '';
        }
    }

    renderEventDetails(details, originalEvent) {
        if (!this.eventDetailsBody) {
            return;
        }

        if (!details) {
            this.eventDetailsBody.innerHTML = `
                <div class="py-12 text-center space-y-3">
                    <i class="fas fa-circle-info text-3xl text-purple-400"></i>
                    <p class="text-gray-200 font-semibold">No additional metadata available</p>
                </div>
            `;
            return;
        }

        const source = details.source || 'Events';
        const eventType = details.eventType || originalEvent?.type || 'Unknown';
        const displayName = details.processName || originalEvent?.processName || 'Unknown process';
        const pid = details.pid || originalEvent?.pid || 'N/A';
        const timestamp = this.formatDateTime(details.timestamp || originalEvent?.timestamp);
        const runningBadge = details.isProcessRunning
            ? '<span class="inline-flex items-center gap-2 text-green-400 font-semibold"><i class="fas fa-circle text-[8px]"></i>Running</span>'
            : '<span class="inline-flex items-center gap-2 text-gray-400 font-semibold"><i class="fas fa-circle text-[8px]"></i>Not Running</span>';

        const processControls = this.renderProcessControlPanel(details, originalEvent);

        const summary = `
            <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
                <div class="glass-panel p-5 rounded-xl border border-purple-500/20">
                    <div class="text-xs uppercase text-gray-500 tracking-[0.25em] mb-2">Feature</div>
                    <div class="text-lg font-semibold text-white">${this.escapeHtml(source)}</div>
                    <div class="text-xs uppercase text-gray-500 mt-3">Event Type</div>
                    <div class="text-sm text-gray-300">${this.escapeHtml(eventType)}</div>
                </div>
                <div class="glass-panel p-5 rounded-xl border border-blue-500/20">
                    <div class="text-xs uppercase text-gray-500 tracking-[0.25em] mb-2">Process</div>
                    <div class="text-lg font-semibold text-white break-words">${this.escapeHtml(displayName)}</div>
                    <div class="mt-3 grid grid-cols-2 gap-3 text-sm">
                        <div>
                            <div class="text-xs uppercase text-gray-500">PID</div>
                            <div class="text-gray-300">${this.escapeHtml(String(pid))}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Status</div>
                            <div data-process-status>${runningBadge}</div>
                        </div>
                    </div>
                </div>
                <div class="glass-panel p-5 rounded-xl border border-emerald-500/20">
                    <div class="text-xs uppercase text-gray-500 tracking-[0.25em] mb-2">Timestamp</div>
                    <div class="text-sm text-gray-300 break-words">${this.escapeHtml(timestamp)}</div>
                </div>
            </div>
        `;

        const executableSection = this.renderFileDetails('Executable', details.executable, true);
        const targetSection = this.renderFileDetails('Target Resource', details.target, false);
        let targetRawSection = '';
        if (details.targetRaw) {
            targetRawSection = `
                <div class="glass-panel p-5 rounded-xl border border-gray-700/40">
                    <div class="text-xs uppercase text-gray-500 tracking-[0.25em] mb-2">Event Data</div>
                    <pre class="bg-gray-900/60 border border-gray-700/40 rounded-lg p-4 text-xs text-gray-200 whitespace-pre-wrap break-all">${this.escapeHtml(details.targetRaw)}</pre>
                </div>
            `;
        }

        let notesSection = '';
        const globalNotes = Array.isArray(details.notes) ? details.notes.filter(n => !!n) : [];
        if (globalNotes.length) {
            notesSection = `
                <div class="glass-panel p-5 rounded-xl border border-amber-500/40">
                    <div class="text-xs uppercase text-amber-300 tracking-[0.25em] mb-2">Notes</div>
                    <ul class="space-y-2 text-sm text-amber-100">
                        ${globalNotes.map(note => `<li class="flex items-start gap-2"><i class="fas fa-info-circle mt-1 text-amber-300"></i><span>${this.escapeHtml(note)}</span></li>`).join('')}
                    </ul>
                </div>
            `;
        }

        this.eventDetailsBody.innerHTML = `
            <div class="space-y-6">
                ${summary}
                ${processControls}
                ${executableSection}
                ${targetSection}
                ${targetRawSection}
                ${notesSection}
            </div>
        `;

        this.eventDetailsBody.querySelectorAll('[data-reveal-path]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const targetPath = btn.dataset.revealPath;
                this.revealPath(targetPath, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-copy-text]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const text = btn.dataset.copyText;
                this.copyToClipboard(text, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-terminate-pid]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const pidValue = btn.dataset.terminatePid;
                this.terminateProcess(pidValue, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-vt-refresh-hash]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const hash = btn.dataset.vtRefreshHash;
                this.refreshVirusTotalReport(hash, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-vt-rescan-hash]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const hash = btn.dataset.vtRescanHash;
                this.requestVirusTotalRescan(hash, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-vt-upload-path]').forEach(btn => {
            btn.addEventListener('click', (evt) => {
                evt.stopPropagation();
                const path = btn.dataset.vtUploadPath;
                this.uploadFileToVirusTotal(path, btn);
            });
        });

        this.eventDetailsBody.querySelectorAll('[data-process-panel]').forEach(panel => {
            const pid = parseInt(panel.dataset.pid, 10);
            panel.querySelectorAll('.process-action-btn').forEach(btn => {
                btn.addEventListener('click', (evt) => {
                    evt.stopPropagation();
                    const action = btn.dataset.action;
                    this.handleProcessAction(action, pid, btn, panel);
                });
            });
        });
    }

    renderFileDetails(title, file, showSignature = false) {
        if (!file) {
            return '';
        }

        const existsBadge = file.exists
            ? '<span class="inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold bg-green-500/10 border border-green-500/30 text-green-300"><i class="fas fa-check-circle"></i>Available</span>'
            : '<span class="inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold bg-red-500/10 border border-red-500/30 text-red-300"><i class="fas fa-times-circle"></i>Missing</span>';

        let signatureBadge = '';
        if (showSignature) {
            signatureBadge = file.isSigned
                ? '<span class="inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold bg-green-500/10 border border-green-500/30 text-green-300"><i class="fas fa-certificate"></i>Signed</span>'
                : '<span class="inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold bg-amber-500/10 border border-amber-500/30 text-amber-300"><i class="fas fa-triangle-exclamation"></i>Unsigned</span>';
        }

        const sizeLabel = file.isDir ? 'Directory' : (typeof file.size === 'number' && file.size >= 0 ? this.formatBytes(file.size) : 'Unknown');
        const modified = file.modified ? this.formatDateTime(file.modified) : 'Unknown';
        const created = file.created ? this.formatDateTime(file.created) : '';
        const firstSeen = file.firstSeen ? this.formatDateTime(file.firstSeen) : '';
        const vtBlock = this.renderVirusTotalDetails(file);
        const peBlock = this.renderPEAnalysis(file.pe, file);
        const stringsBlock = this.renderStringsSummary(file.strings, file);

        let hashBlock = '';
        if (file.hashAvailable && file.sha256) {
            hashBlock = `
                <div class="flex items-center justify-between gap-3 bg-gray-900/60 border border-gray-700/40 rounded-lg px-4 py-3">
                    <div>
                        <div class="text-xs uppercase text-gray-500">SHA-256</div>
                        <div class="font-mono text-xs text-gray-200 break-all">${this.escapeHtml(file.sha256)}</div>
                    </div>
                    <button class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-gray-800/70 border border-gray-700/60 text-xs text-gray-300 hover:text-white hover:border-purple-400/50 transition" data-copy-text="${this.escapeHtml(file.sha256)}">
                        <i class="fas fa-copy"></i>
                        Copy
                    </button>
                </div>
            `;
        } else if (file.hashSkippedReason) {
            hashBlock = `
                <div class="text-xs text-amber-300 bg-amber-500/10 border border-amber-500/30 rounded-lg px-4 py-3">
                    ${this.escapeHtml(file.hashSkippedReason)}
                </div>
            `;
        }

        let notesBlock = '';
        if (Array.isArray(file.notes) && file.notes.length) {
            notesBlock = `
                <div class="mt-4 space-y-2 text-xs text-gray-400">
                    ${file.notes.map(note => `<div class="flex items-start gap-2"><i class="fas fa-info-circle mt-1"></i><span>${this.escapeHtml(note)}</span></div>`).join('')}
                </div>
            `;
        }

        const actions = file.exists ? `
            <div class="flex flex-wrap gap-2 mt-4">
                <button class="inline-flex items-center gap-2 px-3 py-2 rounded-lg bg-gray-800/70 border border-gray-700/60 text-xs font-semibold text-gray-200 hover:text-white hover:border-purple-400/50 transition" data-reveal-path="${this.escapeHtml(file.path)}">
                    <i class="fas fa-folder-open"></i>
                    Open in Explorer
                </button>
                <button class="inline-flex items-center gap-2 px-3 py-2 rounded-lg bg-gray-800/70 border border-gray-700/60 text-xs font-semibold text-gray-200 hover:text-white hover:border-purple-400/50 transition" data-copy-text="${this.escapeHtml(file.path)}">
                    <i class="fas fa-copy"></i>
                    Copy Path
                </button>
            </div>
        ` : '';

        return `
            <div class="glass-panel p-5 rounded-xl border border-gray-700/40 space-y-4">
                <div class="flex flex-wrap items-center justify-between gap-3">
                    <h4 class="text-xl font-semibold text-white">${this.escapeHtml(title)}</h4>
                    <div class="flex flex-wrap items-center gap-2">
                        ${existsBadge}
                        ${signatureBadge}
                    </div>
                </div>
                <div class="space-y-3 text-sm text-gray-300">
                    <div>
                        <div class="text-xs uppercase text-gray-500">Path</div>
                        <div class="font-mono text-sm break-all text-gray-200">${this.escapeHtml(file.path || 'Unknown')}</div>
                    </div>
                    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
                        <div>
                            <div class="text-xs uppercase text-gray-500">Type</div>
                            <div>${file.isDir ? 'Directory' : 'File'}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Size</div>
                            <div>${this.escapeHtml(sizeLabel)}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Modified</div>
                            <div>${this.escapeHtml(modified)}</div>
                        </div>
                    </div>
                    ${created ? `
                        <div>
                            <div class="text-xs uppercase text-gray-500">Created</div>
                            <div>${this.escapeHtml(created)}</div>
                        </div>
                    ` : ''}
                    ${firstSeen ? `
                        <div>
                            <div class="text-xs uppercase text-gray-500">First Seen</div>
                            <div>${this.escapeHtml(firstSeen)}</div>
                        </div>
                    ` : ''}
                    ${hashBlock}
                    ${peBlock}
                    ${stringsBlock}
                    ${vtBlock}
                </div>
                ${actions}
                ${notesBlock}
            </div>
        `;
    }

    renderVirusTotalDetails(file) {
        if (!file) {
            return '';
        }

        const rawHash = typeof file.sha256 === 'string' ? file.sha256 : '';
        const hash = rawHash ? this.escapeHtml(rawHash) : '';
        const report = file.virusTotal || null;
        const status = report?.status || 'not_checked';

        let statusLabel = 'Not Checked';
        let statusClass = 'text-gray-300';
        if (status === 'found') {
            statusLabel = 'Found on VirusTotal';
            statusClass = 'text-emerald-300';
        } else if (status === 'not_found') {
            statusLabel = 'Not Present on VirusTotal';
            statusClass = 'text-amber-300';
        } else if (status === 'queued') {
            statusLabel = 'Rescan Queued';
            statusClass = 'text-blue-300';
        }

        const statsGrid = report ? `
            <div class="grid grid-cols-2 md:grid-cols-4 gap-4 text-center">
                <div class="space-y-1">
                    <div class="text-xs uppercase text-gray-500">Malicious</div>
                    <div class="text-lg font-bold text-red-300">${report.malicious ?? 0}</div>
                </div>
                <div class="space-y-1">
                    <div class="text-xs uppercase text-gray-500">Suspicious</div>
                    <div class="text-lg font-bold text-amber-300">${report.suspicious ?? 0}</div>
                </div>
                <div class="space-y-1">
                    <div class="text-xs uppercase text-gray-500">Undetected</div>
                    <div class="text-lg font-bold text-emerald-300">${report.undetected ?? 0}</div>
                </div>
                <div class="space-y-1">
                    <div class="text-xs uppercase text-gray-500">Harmless</div>
                    <div class="text-lg font-bold text-sky-300">${report.harmless ?? 0}</div>
                </div>
            </div>
        ` : `
            <p class="text-sm text-gray-400">No VirusTotal report yet. Click <span class="font-semibold">Refresh Report</span> to query using the SHA-256 hash.</p>
        `;

        const lastAnalysisValue = report?.lastAnalysisDate ? this.escapeHtml(this.formatDateTime(report.lastAnalysisDate)) : 'No analysis date available';
        const lastAnalysis = `
            <div class="text-xs text-gray-400" data-vt-last-analysis>${lastAnalysisValue}</div>
        `;

        const notes = (report?.notes || []).map(note => `
            <li class="flex items-start gap-2 text-xs text-gray-400"><i class="fas fa-info-circle mt-1"></i><span>${this.escapeHtml(note)}</span></li>
        `).join('');

        const notesBlock = notes ? `<ul class="mt-3 space-y-2" data-vt-notes>${notes}</ul>` : '<ul class="mt-3 space-y-2 hidden" data-vt-notes></ul>';

        const link = report?.link ? this.escapeHtml(report.link) : `https://www.virustotal.com/gui/file/${hash}/detection`;

        return `
            <div class="border border-purple-500/30 rounded-lg px-4 py-4 bg-gray-900/40 space-y-3" data-vt-card data-vt-hash="${hash}" data-vt-path="${this.escapeHtml(file.path || '')}">
                <div class="flex flex-wrap items-center justify-between gap-2">
                    <div>
                        <div class="text-xs uppercase text-gray-500 tracking-[0.3em]">VirusTotal</div>
                        <div class="text-sm font-semibold ${statusClass}" data-vt-status>${statusLabel}</div>
                    </div>
                    <div class="flex flex-wrap items-center gap-2">
                        <button class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-gray-800/70 border border-gray-700/60 text-xs font-semibold text-gray-200 hover:text-white hover:border-purple-400/50 transition" data-vt-refresh-hash="${hash}" ${hash ? '' : 'disabled'}>
                            <i class="fas fa-sync"></i>
                            Refresh Report
                        </button>
                        <button class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-blue-500/20 border border-blue-500/40 text-xs font-semibold text-blue-200 hover:text-white hover:border-blue-400/60 transition" data-vt-rescan-hash="${hash}" ${hash ? '' : 'disabled'}>
                            <i class="fas fa-redo"></i>
                            Request Rescan
                        </button>
                        <button class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-purple-500/20 border border-purple-500/40 text-xs font-semibold text-purple-200 hover:text-white hover:border-purple-400/60 transition" data-vt-upload-path="${this.escapeHtml(file.path || '')}" ${file.exists && !file.isDir ? '' : 'disabled'}>
                            <i class="fas fa-upload"></i>
                            Upload to VirusTotal
                        </button>
                        <a class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-gray-800/70 border border-gray-700/60 text-xs font-semibold text-gray-200 hover:text-white hover:border-purple-400/50 transition" href="${link}" target="_blank" rel="noopener">
                            <i class="fas fa-external-link-alt"></i>
                            View on VirusTotal
                        </a>
                    </div>
                </div>
                <div data-vt-stats>${statsGrid}</div>
                ${lastAnalysis}
                ${notesBlock}
            </div>
        `;
    }

    renderPEAnalysis(pe, file) {
        if (!pe) {
            return '';
        }

        const architecture = this.escapeHtml(pe.architecture || 'Unknown');
        const type = this.escapeHtml(pe.fileType || 'Unknown');
        const entryPoint = this.escapeHtml(pe.entryPoint || 'Unknown');
        const imageBase = this.escapeHtml(pe.imageBase || 'Unknown');
        const sectionCount = typeof pe.numberOfSections === 'number' ? pe.numberOfSections : (pe.sections?.length || 0);
        const sizeOfImage = pe.sizeOfImage ? this.escapeHtml(pe.sizeOfImage) : 'Unknown';
        const importCount = typeof pe.importCount === 'number' ? pe.importCount : 0;
        const exportCount = typeof pe.exportCount === 'number' ? pe.exportCount : 0;
        const fileSizeText = typeof file?.size === 'number' ? this.formatBytes(file.size) : 'Unknown';

        const sections = (pe.sections || []).map(section => {
            const entropyValue = typeof section.entropy === 'number' ? section.entropy.toFixed(2) : '—';
            const entropyBadge = this.getEntropyBadge(section.entropyLabel, entropyValue);
            const riskBadge = this.getRiskBadge(section.risk);
            const permissions = Array.isArray(section.permissions) && section.permissions.length
                ? section.permissions.map(p => this.escapeHtml(p)).join(', ')
                : '—';
            return `
                <tr class="border-t border-gray-800/40">
                    <td class="px-3 py-2 font-mono text-sm text-gray-200">${this.escapeHtml(section.name || 'Unnamed')}</td>
                    <td class="px-3 py-2 font-mono text-xs text-gray-400">${this.escapeHtml(section.virtualSize || '0x0')}</td>
                    <td class="px-3 py-2 font-mono text-xs text-gray-400">${this.escapeHtml(section.virtualAddress || '0x0')}</td>
                    <td class="px-3 py-2 font-mono text-xs text-gray-400">${this.escapeHtml(section.rawSize || '0x0')}</td>
                    <td class="px-3 py-2">${entropyBadge}</td>
                    <td class="px-3 py-2 text-xs text-gray-300">${permissions}</td>
                    <td class="px-3 py-2">${riskBadge}</td>
                </tr>
            `;
        }).join('');

        const sectionsTable = sections ? `
            <div class="overflow-x-auto border border-gray-800/40 rounded-lg">
                <table class="min-w-full text-left">
                    <thead class="bg-gray-900/60 text-xs uppercase text-gray-500">
                        <tr>
                            <th class="px-3 py-2 font-semibold">Name</th>
                            <th class="px-3 py-2 font-semibold">Virtual Size</th>
                            <th class="px-3 py-2 font-semibold">Virtual Address</th>
                            <th class="px-3 py-2 font-semibold">Raw Size</th>
                            <th class="px-3 py-2 font-semibold">Entropy</th>
                            <th class="px-3 py-2 font-semibold">Permissions</th>
                            <th class="px-3 py-2 font-semibold">Risk</th>
                        </tr>
                    </thead>
                    <tbody class="text-sm">
                        ${sections}
                    </tbody>
                </table>
            </div>
        ` : `
            <div class="p-4 bg-gray-900/40 border border-gray-800/40 rounded-lg text-sm text-gray-400">
                No section data available for this file.
            </div>
        `;

        return `
            <div class="space-y-4">
                <div class="glass-panel p-5 rounded-xl border border-purple-500/30 space-y-4 bg-gray-900/30">
                    <div class="flex flex-col md:flex-row md:items-center md:justify-between gap-4">
                        <div>
                            <div class="text-xs uppercase text-gray-500 tracking-[0.3em]">PE Analysis</div>
                            <div class="text-lg font-semibold text-white">Portable Executable Insights</div>
                        </div>
                        <div class="text-sm text-gray-400">
                            File Size: <span class="text-gray-200 font-semibold">${fileSizeText}</span>
                        </div>
                    </div>
                    <div class="grid grid-cols-1 md:grid-cols-3 gap-4 text-sm text-gray-300">
                        <div>
                            <div class="text-xs uppercase text-gray-500">Architecture</div>
                            <div>${architecture}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Type</div>
                            <div>${type}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Entry Point</div>
                            <div>${entryPoint}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Image Base</div>
                            <div>${imageBase}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Sections</div>
                            <div>${sectionCount}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Size of Image</div>
                            <div>${sizeOfImage}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Imported Symbols</div>
                            <div>${importCount}</div>
                        </div>
                        <div>
                            <div class="text-xs uppercase text-gray-500">Exported Symbols</div>
                            <div>${exportCount}</div>
                        </div>
                    </div>
                    ${sectionsTable}
                </div>
            </div>
        `;
    }

    renderStringsSummary(stringsSummary, file) {
        if (!stringsSummary || typeof stringsSummary !== 'object') {
            return '';
        }

        const count = typeof stringsSummary.count === 'number' ? stringsSummary.count : (stringsSummary.Count || 0);
        const samples = stringsSummary.samples || stringsSummary.Samples || [];
        const keywordsMap = stringsSummary.keywords || stringsSummary.Keywords || {};

        const sampleItems = samples.map(sample => `
            <li class="font-mono text-xs text-gray-200 break-all">${this.escapeHtml(sample)}</li>
        `).join('');

        const keywordEntries = Object.entries(keywordsMap || {});
        const keywordSection = keywordEntries.length ? `
            <div class="space-y-2">
                <div class="text-xs uppercase text-gray-500">Keyword Matches</div>
                <div class="space-y-2 max-h-40 overflow-y-auto pr-2 custom-scrollbars">
                    ${keywordEntries.map(([kw, matches]) => `
                        <div>
                            <div class="text-xs font-semibold text-blue-300">${this.escapeHtml(kw)} <span class="text-gray-500">(${matches.length})</span></div>
                            <div class="font-mono text-[11px] text-gray-300 break-all">${matches.slice(0, 5).map(m => this.escapeHtml(m)).join('<span class=\"text-gray-600\"> • </span>')}</div>
                        </div>
                    `).join('')}
                </div>
            </div>
        ` : '';

        const sampleList = sampleItems ? `
            <ul class="space-y-1 max-h-56 overflow-y-auto pr-2 custom-scrollbars">${sampleItems}</ul>
        ` : '<p class="text-xs text-gray-400">No printable strings detected.</p>';

        return `
            <div class="glass-panel p-5 rounded-xl border border-green-500/30 bg-green-900/10 space-y-4">
                <div class="flex flex-col md:flex-row md:items-center md:justify-between gap-3">
                    <div>
                        <div class="text-xs uppercase text-green-300 tracking-[0.3em]">String Intelligence</div>
                        <div class="text-lg font-semibold text-white">Printable Strings Overview</div>
                    </div>
                    <div class="flex items-center gap-2">
                        <span class="text-xs text-gray-400">Total</span>
                        <span class="text-lg font-semibold text-green-200">${count}</span>
                    </div>
                </div>
                <div class="flex flex-col gap-4">
                    <div>
                        <div class="text-xs uppercase text-gray-500 mb-2">Search Strings</div>
                        <div class="flex items-center gap-2">
                            <input type="text" class="strings-search-input w-full px-3 py-2 rounded-lg bg-gray-900/60 border border-gray-700/60 text-sm text-gray-200 focus:outline-none focus:ring-2 focus:ring-green-500/60 focus:border-green-400/60 transition" placeholder="Search for keywords..." data-file-path="${this.escapeHtml(file.path || '')}" />
                            <button class="strings-search-btn inline-flex items-center gap-2 px-3 py-2 rounded-lg bg-green-500/20 border border-green-500/40 text-xs font-semibold text-green-200 hover:text-white hover:border-green-400/60 transition" data-file-path="${this.escapeHtml(file.path || '')}">
                                <i class="fas fa-search"></i>
                                Search
                            </button>
                        </div>
                        <p class="text-[11px] text-gray-500 mt-1">Enter keywords to highlight matches in the sample list below (case-insensitive).</p>
                    </div>
                    <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
                        <div class="space-y-2">
                            <div class="text-xs uppercase text-gray-500">Sample Strings</div>
                            <div class="strings-sample-list" data-file-path="${this.escapeHtml(file.path || '')}">
                                ${sampleList}
                            </div>
                        </div>
                        ${keywordSection}
                    </div>
                </div>
            </div>
        `;
    }

    getEntropyBadge(level, value) {
        const label = typeof level === 'string' ? level.toLowerCase() : 'low';
        let classes = 'border-emerald-500/40 bg-emerald-500/10 text-emerald-200';
        if (label === 'high') {
            classes = 'border-red-500/40 bg-red-500/10 text-red-300';
        } else if (label === 'medium') {
            classes = 'border-amber-500/40 bg-amber-500/10 text-amber-200';
        }
        const valueText = this.escapeHtml(String(value ?? '—'));
        const labelText = this.escapeHtml((level || 'Low').toUpperCase());
        return `<span class="inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold ${classes}">${valueText}<span>${labelText}</span></span>`;
    }

    getRiskBadge(level) {
        const risk = (level || 'Low').toLowerCase();
        let classes = 'border-emerald-500/40 bg-emerald-500/10 text-emerald-200';
        if (risk === 'high') {
            classes = 'border-red-500/40 bg-red-500/10 text-red-300';
        } else if (risk === 'medium') {
            classes = 'border-amber-500/40 bg-amber-500/10 text-amber-200';
        }
        return `<span class="inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold ${classes}">${this.escapeHtml(level || 'Low')}</span>`;
    }

    renderEventDetailsLoading() {
        return `
            <div class="flex flex-col items-center justify-center py-12 space-y-4 text-gray-400">
                <div class="w-12 h-12 border-4 border-purple-500/40 border-t-transparent rounded-full animate-spin"></div>
                <div class="text-sm font-semibold">Gathering event metadata…</div>
            </div>
        `;
    }

    formatDateTime(value) {
        if (!value) {
            return 'Unknown';
        }
        const date = new Date(value);
        if (Number.isNaN(date.getTime())) {
            return value;
        }
        return `${date.toLocaleDateString()} ${date.toLocaleTimeString()}`;
    }

    formatBytes(bytes) {
        if (typeof bytes !== 'number' || !Number.isFinite(bytes) || bytes < 0) {
            return 'Unknown';
        }
        const units = ['B', 'KB', 'MB', 'GB', 'TB'];
        let size = bytes;
        let unitIndex = 0;
        while (size >= 1024 && unitIndex < units.length - 1) {
            size /= 1024;
            unitIndex++;
        }
        const precision = unitIndex === 0 ? 0 : (size >= 10 ? 1 : 2);
        return `${size.toFixed(precision)} ${units[unitIndex]}`;
    }

    async revealPath(path, button) {
        if (!path) {
            return;
        }
        const btn = button;
        try {
            if (btn) {
                btn.disabled = true;
                btn.classList.add('opacity-60');
            }
            if (window.go?.app?.App?.RevealPath) {
                await window.go.app.App.RevealPath(path);
            } else if (window.RevealPath) {
                await window.RevealPath(path);
            } else {
                alert('RevealPath binding not available in this build.');
            }
        } catch (error) {
            console.error('Reveal path error:', error);
            const message = error?.message || error?.toString() || 'Failed to open path.';
            alert('Failed to open path: ' + message);
        } finally {
            if (btn) {
                btn.disabled = false;
                btn.classList.remove('opacity-60');
            }
        }
    }

    async terminateProcess(pid, button) {
        const numericPid = parseInt(pid, 10);
        if (!numericPid || Number.isNaN(numericPid)) {
            alert('Invalid process identifier.');
            return;
        }

        const confirmTermination = confirm(`Terminate process PID ${numericPid}?`);
        if (!confirmTermination) {
            return;
        }

        const btn = button;
        const originalContent = btn ? btn.innerHTML : '';

        try {
            if (btn) {
                btn.disabled = true;
                btn.innerHTML = '<i class="fas fa-spinner fa-spin text-red-300"></i> Terminating…';
            }

            if (window.go?.app?.App?.TerminateProcess) {
                await window.go.app.App.TerminateProcess(numericPid);
            } else if (window.TerminateProcess) {
                await window.TerminateProcess(numericPid);
            } else {
                throw new Error('TerminateProcess binding not available in this build.');
            }

            if (btn) {
                btn.innerHTML = '<i class="fas fa-check text-green-400"></i> Terminated';
                btn.classList.add('opacity-80');
            }

            const statusContainer = this.eventDetailsBody?.querySelector('[data-process-status]');
            if (statusContainer) {
                statusContainer.innerHTML = '<span class="inline-flex items-center gap-2 text-gray-400 font-semibold"><i class="fas fa-circle text-[8px]"></i>Not Running</span>';
            }
        } catch (error) {
            console.error('Terminate process error:', error);
            if (btn) {
                btn.disabled = false;
                btn.innerHTML = originalContent || '<i class="fas fa-skull-crossbones"></i> Terminate Process';
            }
            alert('Failed to terminate process: ' + (error?.message || error));
        }
    }

    async refreshVirusTotalReport(hash, button) {
        if (!hash) {
            return;
        }

        const card = button?.closest('[data-vt-card]');
        const originalContent = button ? button.innerHTML : '';

        try {
            if (button) {
                button.disabled = true;
                button.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Refreshing…';
            }

            let report = null;
            if (window.go?.app?.App?.LookupVirusTotal) {
                report = await window.go.app.App.LookupVirusTotal(hash);
            } else if (window.LookupVirusTotal) {
                report = await window.LookupVirusTotal(hash);
            } else {
                throw new Error('LookupVirusTotal binding not available in this build.');
            }

            this.applyVirusTotalReport(card, report);
        } catch (error) {
            console.error('VirusTotal lookup error:', error);
            alert('Failed to refresh VirusTotal report: ' + (error?.message || error));
        } finally {
            if (button) {
                button.disabled = false;
                button.innerHTML = originalContent || '<i class="fas fa-sync"></i> Refresh Report';
            }
        }
    }

    async requestVirusTotalRescan(hash, button) {
        if (!hash) {
            return;
        }

        const confirmRescan = confirm('Request a VirusTotal rescan for this hash?');
        if (!confirmRescan) {
            return;
        }

        const card = button?.closest('[data-vt-card]');
        const originalContent = button ? button.innerHTML : '';

        try {
            if (button) {
                button.disabled = true;
                button.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Requesting…';
            }

            let report = null;
            if (window.go?.app?.App?.RescanVirusTotal) {
                report = await window.go.app.App.RescanVirusTotal(hash);
            } else if (window.RescanVirusTotal) {
                report = await window.RescanVirusTotal(hash);
            } else {
                throw new Error('RescanVirusTotal binding not available in this build.');
            }

            this.applyVirusTotalReport(card, report);
        } catch (error) {
            console.error('VirusTotal rescan error:', error);
            alert('Failed to request VirusTotal rescan: ' + (error?.message || error));
        } finally {
            if (button) {
                button.disabled = false;
                button.innerHTML = originalContent || '<i class="fas fa-redo"></i> Request Rescan';
            }
        }
    }

    async uploadFileToVirusTotal(path, button) {
        if (!path) {
            alert('No file path available to upload.');
            return;
        }

        const confirmUpload = confirm('Upload this file to VirusTotal? The file contents will be shared with VirusTotal.');
        if (!confirmUpload) {
            return;
        }

        const card = button?.closest('[data-vt-card]');
        const originalContent = button ? button.innerHTML : '';

        try {
            if (button) {
                button.disabled = true;
                button.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Uploading…';
            }

            let report = null;
            if (window.go?.app?.App?.UploadVirusTotal) {
                report = await window.go.app.App.UploadVirusTotal(path);
            } else if (window.UploadVirusTotal) {
                report = await window.UploadVirusTotal(path);
            } else {
                throw new Error('UploadVirusTotal binding not available in this build.');
            }

            this.applyVirusTotalReport(card, report);
            this.showSettingsStatus('', 'idle');
        } catch (error) {
            console.error('VirusTotal upload error:', error);
            alert('Failed to upload file to VirusTotal: ' + (error?.message || error));
        } finally {
            if (button) {
                button.disabled = false;
                button.innerHTML = originalContent || '<i class="fas fa-upload"></i> Upload to VirusTotal';
            }
        }
    }

    applyVirusTotalReport(card, report) {
        if (!card || !report) {
            return;
        }
        const statusEl = card.querySelector('[data-vt-status]');
        const statsEl = card.querySelector('[data-vt-stats]');
        const linkEl = card.querySelector('a[href*="virustotal.com"]');
        const uploadBtn = card.querySelector('[data-vt-upload-path]');

        if (report.hash) {
            card.dataset.vtHash = report.hash;
        }

        const status = (report.status || 'not_checked').toLowerCase();
        let statusLabel = 'Not Checked';
        let statusClass = 'text-gray-300';
        if (status === 'found') {
            statusLabel = 'Found on VirusTotal';
            statusClass = 'text-emerald-300';
        } else if (status === 'not_found') {
            statusLabel = 'Not Present on VirusTotal';
            statusClass = 'text-amber-300';
        } else if (status === 'queued') {
            statusLabel = 'Rescan Queued';
            statusClass = 'text-blue-300';
        }

        if (statusEl) {
            statusEl.textContent = statusLabel;
            statusEl.className = `text-sm font-semibold ${statusClass}`;
        }

        if (statsEl) {
            if (status === 'found' || status === 'queued') {
                statsEl.innerHTML = `
                    <div class="grid grid-cols-2 md:grid-cols-4 gap-4 text-center">
                        <div class="space-y-1">
                            <div class="text-xs uppercase text-gray-500">Malicious</div>
                            <div class="text-lg font-bold text-red-300">${report.malicious ?? 0}</div>
                        </div>
                        <div class="space-y-1">
                            <div class="text-xs uppercase text-gray-500">Suspicious</div>
                            <div class="text-lg font-bold text-amber-300">${report.suspicious ?? 0}</div>
                        </div>
                        <div class="space-y-1">
                            <div class="text-xs uppercase text-gray-500">Undetected</div>
                            <div class="text-lg font-bold text-emerald-300">${report.undetected ?? 0}</div>
                        </div>
                        <div class="space-y-1">
                            <div class="text-xs uppercase text-gray-500">Harmless</div>
                            <div class="text-lg font-bold text-sky-300">${report.harmless ?? 0}</div>
                        </div>
                    </div>
                `;
            } else {
                statsEl.innerHTML = '<p class="text-sm text-gray-400">No VirusTotal report yet. Click Refresh Report to query using the SHA-256 hash.</p>';
            }
        }

        if (linkEl && report.link) {
            linkEl.href = report.link;
        }

        const lastAnalysisEl = card.querySelector('[data-vt-last-analysis]');
        if (lastAnalysisEl) {
            lastAnalysisEl.textContent = report.lastAnalysisDate ? this.formatDateTime(report.lastAnalysisDate) : 'No analysis date available';
        }

        const notesContainer = card.querySelector('[data-vt-notes]');
        if (notesContainer) {
            notesContainer.innerHTML = '';
            if (report.notes && report.notes.length) {
                notesContainer.classList.remove('hidden');
                report.notes.forEach(note => {
                    const item = document.createElement('li');
                    item.className = 'flex items-start gap-2 text-xs text-gray-400';
                    item.innerHTML = '<i class="fas fa-info-circle mt-1"></i><span>' + this.escapeHtml(note) + '</span>';
                    notesContainer.appendChild(item);
                });
            } else {
                notesContainer.classList.add('hidden');
            }
        }

        if (uploadBtn) {
            if (status === 'found') {
                uploadBtn.disabled = true;
                uploadBtn.innerHTML = '<i class="fas fa-check text-green-300"></i> Uploaded';
                uploadBtn.classList.add('opacity-60');
            } else if (status === 'queued') {
                uploadBtn.disabled = true;
                uploadBtn.innerHTML = '<i class="fas fa-hourglass-half text-blue-300"></i> Processing';
                uploadBtn.classList.add('opacity-60');
            } else {
                uploadBtn.disabled = false;
                uploadBtn.innerHTML = '<i class="fas fa-upload"></i> Upload to VirusTotal';
                uploadBtn.classList.remove('opacity-60');
            }
        }
    }

    async copyToClipboard(text, button) {
        if (!text) {
            return;
        }
        try {
            await navigator.clipboard.writeText(text);
            if (button) {
                const original = button.innerHTML;
                button.innerHTML = '<i class="fas fa-check text-green-400"></i>Copied';
                button.classList.add('text-green-300');
                setTimeout(() => {
                    button.innerHTML = original;
                    button.classList.remove('text-green-300');
                }, 1600);
            }
        } catch (error) {
            console.error('Clipboard error:', error);
            alert('Failed to copy to clipboard.');
        }
    }

    renderProcessControlPanel(details, originalEvent) {
        const pid = details?.pid || originalEvent?.pid;
        const processName = details?.processName || originalEvent?.processName || 'Unknown process';
        const isRunning = !!details?.isProcessRunning;

        if (!pid) {
            return '';
        }

        const statusBadge = isRunning
            ? '<span class="inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold bg-emerald-500/10 border border-emerald-500/30 text-emerald-200"><i class="fas fa-circle text-[6px]"></i>Running</span>'
            : '<span class="inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold bg-gray-500/10 border border-gray-500/30 text-gray-300"><i class="fas fa-circle text-[6px]"></i>Stopped</span>';

        return `
            <div class="glass-panel p-5 rounded-xl border border-blue-500/30 bg-blue-900/20 space-y-4" data-process-panel data-pid="${pid}">
                <div class="flex flex-col md:flex-row md:items-center md:justify-between gap-4">
                    <div>
                        <div class="text-xs uppercase text-blue-300 tracking-[0.3em]">Process Control Panel</div>
                        <div class="text-lg font-semibold text-white">${this.escapeHtml(processName)} (PID ${pid})</div>
                    </div>
                    <div>${statusBadge}</div>
                </div>
                <div class="grid grid-cols-1 sm:grid-cols-3 gap-3">
                    <button class="process-action-btn bg-amber-500/20 border border-amber-500/40 text-amber-200 hover:text-white hover:border-amber-400/60" data-action="suspend" ${isRunning ? '' : 'disabled'}>
                        <i class="fas fa-pause-circle"></i>
                        <span>Suspend</span>
                    </button>
                    <button class="process-action-btn bg-emerald-500/20 border border-emerald-500/40 text-emerald-200 hover:text-white hover:border-emerald-400/60" data-action="resume" ${isRunning ? 'disabled' : ''}>
                        <i class="fas fa-play-circle"></i>
                        <span>Resume</span>
                    </button>
                    <button class="process-action-btn bg-red-500/20 border border-red-500/40 text-red-200 hover:text-white hover:border-red-400/60" data-action="terminate">
                        <i class="fas fa-skull-crossbones"></i>
                        <span>Terminate</span>
                    </button>
                </div>
                <p class="text-xs text-blue-200/80">
                    Use these controls to manage the process directly. Suspend pauses execution without killing it; resume continues execution; terminate ends the process entirely.
                </p>
            </div>
        `;
    }

    async handleProcessAction(action, pid, button, panel) {
        if (!pid) {
            alert('Invalid PID for process control.');
            return;
        }

        const originalContent = button ? button.innerHTML : '';
        try {
            if (button) {
                button.disabled = true;
                button.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Working…';
            }

            if (action === 'terminate') {
                await this.terminateProcess(String(pid), button);
            } else if (action === 'suspend') {
                await this.suspendProcess(pid);
            } else if (action === 'resume') {
                await this.resumeProcess(pid);
            }

            this.updateProcessPanelState(action, panel);
        } catch (error) {
            console.error('Process control error:', error);
            alert('Process control failed: ' + (error?.message || error));
        } finally {
            if (button) {
                button.disabled = false;
                button.innerHTML = originalContent || button.innerHTML;
            }
        }
    }

    async suspendProcess(pid) {
        if (window.go?.app?.App?.SuspendProcess) {
            await window.go.app.App.SuspendProcess(pid);
        } else if (window.SuspendProcess) {
            await window.SuspendProcess(pid);
        } else {
            throw new Error('SuspendProcess binding not available in this build.');
        }
    }

    async resumeProcess(pid) {
        if (window.go?.app?.App?.ResumeProcess) {
            await window.go.app.App.ResumeProcess(pid);
        } else if (window.ResumeProcess) {
            await window.ResumeProcess(pid);
        } else {
            throw new Error('ResumeProcess binding not available in this build.');
        }
    }

    updateProcessPanelState(lastAction, panel) {
        if (!panel) {
            return;
        }
        const suspendBtn = panel.querySelector('[data-action="suspend"]');
        const resumeBtn = panel.querySelector('[data-action="resume"]');
        const statusBadge = panel.querySelector('div > div > span');

        if (lastAction === 'terminate') {
            if (suspendBtn) suspendBtn.disabled = true;
            if (resumeBtn) resumeBtn.disabled = true;
            if (statusBadge) {
                statusBadge.className = 'inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold bg-gray-500/10 border border-gray-500/30 text-gray-300';
                statusBadge.innerHTML = '<i class="fas fa-circle text-[6px]"></i>Stopped';
            }
        } else if (lastAction === 'suspend') {
            if (suspendBtn) suspendBtn.disabled = true;
            if (resumeBtn) resumeBtn.disabled = false;
            if (statusBadge) {
                statusBadge.className = 'inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold bg-amber-500/10 border border-amber-500/30 text-amber-200';
                statusBadge.innerHTML = '<i class="fas fa-circle text-[6px]"></i>Suspended';
            }
        } else if (lastAction === 'resume') {
            if (suspendBtn) suspendBtn.disabled = false;
            if (resumeBtn) resumeBtn.disabled = true;
            if (statusBadge) {
                statusBadge.className = 'inline-flex items-center gap-2 px-2 py-1 rounded-lg text-xs font-semibold bg-emerald-500/10 border border-emerald-500/30 text-emerald-200';
                statusBadge.innerHTML = '<i class="fas fa-circle text-[6px]"></i>Running';
            }
        }
    }
}

new NoMoreStealers();
