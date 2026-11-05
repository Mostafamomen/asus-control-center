// SPDX-License-Identifier: GPL-3.0-or-later
//
// Clutter overlay manager for Soft Dimmer (ASUSCraft integration).
// Creates one translucent overlay actor per monitor. Overlays are
// reactive=false so they never block input. Brightness values come from
// the ASUSCraft D-Bus service via SoftDimmerDbusClient.

import Clutter from 'gi://Clutter';
import Meta from 'gi://Meta';

// Duration of brightness transition animations in milliseconds.
const TRANSITION_MS = 250;

// Minimum alpha to avoid a fully invisible overlay when brightness = 1.0.
// We still create the actor but with alpha 0 (no visual effect).
const MIN_ALPHA = 0;
const MAX_ALPHA = 255;

/**
 * MonitorDimmer — manages one Clutter overlay actor per monitor.
 * Brightness values are pushed from the D-Bus client (ASUSCraft engine).
 */
export class MonitorDimmer {
    /**
     * @param {SoftDimmerDbusClient} dbusClient
     */
    constructor(dbusClient) {
        this._dbus = dbusClient;
        this._stage = global.stage;
        this._monitorManager = global.backend.get_monitor_manager();
        this._overlays = new Map(); // monitor index → Clutter.Actor
        this._brightness = new Map(); // monitor index → 0.0–1.0
        this._enabled = false;
        this._monitorsChangedId = 0;
        this._fullscreenIds = [];
    }

    enable() {
        if (this._enabled)
            return;
        this._enabled = true;

        // GNOME 51: use backend.get_monitor_manager()
        this._monitorsChangedId = this._monitorManager.connect(
            'monitors-changed',
            () => this._onMonitorsChanged()
        );

        this._buildOverlays();
        this._applyInitialBrightness();
    }

    disable() {
        if (!this._enabled)
            return;
        this._enabled = false;

        if (this._monitorsChangedId) {
            this._monitorManager.disconnect(this._monitorsChangedId);
            this._monitorsChangedId = 0;
        }

        this._destroyAllOverlays();
    }

    /** Called by the D-Bus client when brightness changes. */
    setBrightness(monitorIndex, brightness) {
        const clamped = Math.max(0.05, Math.min(1.0, brightness));
        this._brightness.set(monitorIndex, clamped);

        if (monitorIndex === -1) {
            // Apply to all monitors.
            for (const idx of this._overlays.keys())
                this._applyBrightness(idx, clamped);
        } else {
            this._applyBrightness(monitorIndex, clamped);
        }
    }

    setEnabled(enabled) {
        this._enabled = enabled;
        if (!enabled) {
            // Full brightness (no overlay effect).
            for (const idx of this._overlays.keys())
                this._applyBrightness(idx, 1.0);
        } else {
            // Re-apply current brightness values.
            for (const [idx, val] of this._brightness.entries())
                this._applyBrightness(idx, val);
        }
    }

    getBrightness(monitorIndex) {
        return this._brightness.get(monitorIndex) ?? 1.0;
    }

    getMonitorCount() {
        return this._overlays.size;
    }

    // ── private ──────────────────────────────────────────────────

    _applyInitialBrightness() {
        // Query the D-Bus service for the current brightness of each monitor.
        // This is asynchronous; we fire and forget, the signal handler
        // (BrightnessChanged) will update overlays as values arrive.
        if (!this._dbus.available)
            return;
        const nMonitors = this._getMonitorCount();
        for (let i = 0; i < nMonitors; i++) {
            this._dbus.getBrightness(i).then(b => {
                this.setBrightness(i, b);
            });
        }
        this._dbus.getBrightness(-1).then(b => {
            this.setBrightness(-1, b);
        });
    }

    _buildOverlays() {
        const monitors = this._getMonitors();
        for (const monitor of monitors)
            this._createOverlay(monitor.index, monitor.geometry);
    }

    _destroyAllOverlays() {
        for (const actor of this._overlays.values()) {
            if (actor.get_parent())
                actor.get_parent().remove_child(actor);
            actor.destroy();
        }
        this._overlays.clear();
        this._brightness.clear();
    }

    _onMonitorsChanged() {
        // Save existing brightness values by index.
        const savedBrightness = new Map(this._brightness);

        // Destroy old overlays.
        for (const actor of this._overlays.values()) {
            if (actor.get_parent())
                actor.get_parent().remove_child(actor);
            actor.destroy();
        }
        this._overlays.clear();
        this._brightness.clear();

        // Rebuild for new monitor configuration.
        const monitors = this._getMonitors();
        for (const monitor of monitors) {
            this._createOverlay(monitor.index, monitor.geometry);
            const prev = savedBrightness.get(monitor.index);
            if (prev !== undefined)
                this._applyBrightness(monitor.index, prev);
        }

        // Re-query brightness from D-Bus.
        this._applyInitialBrightness();
    }

    _createOverlay(monitorIndex, geometry) {
        // GNOME 51: use object literal for color, not new Clutter.Color()
        const actor = new Clutter.Actor({
            x: geometry.x,
            y: geometry.y,
            width: geometry.width,
            height: geometry.height,
            // Never block input.
            reactive: false,
            track_hover: false,
            can_focus: false,
            // Start fully transparent (brightness = 1.0 → alpha = 0).
            background_color: { red: 0, green: 0, blue: 0, alpha: 0 },
            // Position above normal windows but below system chrome.
            z_position: 0,
        });

        // Add to the stage above normal windows.
        this._stage.add_child(actor);
        actor.set_child_above_sibling(null, null);

        this._overlays.set(monitorIndex, actor);
        this._brightness.set(monitorIndex, 1.0);
    }

    _applyBrightness(monitorIndex, brightness) {
        const actor = this._overlays.get(monitorIndex);
        if (!actor)
            return;

        this._brightness.set(monitorIndex, brightness);

        // Alpha = 0 at full brightness, MAX_ALPHA*(1-brightness) at minimum.
        const alpha = Math.round((1.0 - brightness) * MAX_ALPHA);
        // GNOME 51: object literal for color.
        const targetColor = { red: 0, green: 0, blue: 0, alpha };

        actor.save_easing_state();
        actor.set_easing_duration(TRANSITION_MS);
        actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_QUAD);
        actor.background_color = targetColor;
        actor.restore_easing_state();
    }

    _getMonitors() {
        const result = [];
        const nMonitors = this._getMonitorCount();
        for (let i = 0; i < nMonitors; i++) {
            result.push({
                index: i,
                geometry: global.display.get_monitor_geometry(i),
            });
        }
        return result;
    }

    _getMonitorCount() {
        return global.display.get_n_monitors();
    }
}
