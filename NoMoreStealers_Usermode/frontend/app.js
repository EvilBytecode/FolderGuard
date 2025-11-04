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
        this.initTutorial();
        this.updateAntispyStatus();
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
        
        const eventsList = document.getElementById('eventsList');
        const eventCard = this.createEventCard(event);
        if (eventsList) eventsList.insertBefore(eventCard, eventsList.firstChild);
        if (this.events.length > 100) {
            this.events.pop();
            if (eventsList?.lastChild) eventsList.removeChild(eventsList.lastChild);
        }
        requestAnimationFrame(() => eventCard.classList.add('fade-in'));
    }

    /**
     * Create a DOM card for an event.
     * @param {object} event
     * @returns {HTMLElement}
     */
    createEventCard(event) {
        const card = document.createElement('div');
        card.className = 'event-card rounded-2xl p-6 gradient-border';

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

        return card;
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
     * Initialize tab buttons for Events / Credits.
     */
    initTabs() {
        const tabBtns = document.querySelectorAll('.tab-btn');
        const eventsTab = document.getElementById('eventsTab');
        const aboutTab = document.getElementById('aboutTab');
        const creditsTab = document.getElementById('creditsTab');

        tabBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                const tab = btn.dataset.tab;
                tabBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                eventsTab.classList.add('hidden');
                aboutTab.classList.add('hidden');
                creditsTab.classList.add('hidden');
                if (tab === 'events') {
                    eventsTab.classList.remove('hidden');
                } else if (tab === 'about') {
                    aboutTab.classList.remove('hidden');
                } else if (tab === 'credits') {
                    creditsTab.classList.remove('hidden');
                }
            });
        });
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
}

new NoMoreStealers();
