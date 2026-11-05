// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quick Settings indicator: brightness slider + enable/disable toggle.
// Communicates with the ASUSCraft D-Bus service via SoftDimmerDbusClient.

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import St from 'gi://St';
import Clutter from 'gi://Clutter';

import {
    QuickToggle,
    SystemIndicator,
} from 'resource:///org/gnome/shell/ui/quickSettings.js';

import {Slider} from 'resource:///org/gnome/shell/ui/slider.js';

// ── Toggle button ─────────────────────────────────────────────────────────────

const SoftDimmerToggle = GObject.registerClass(
class SoftDimmerToggle extends QuickToggle {
    _init(dbusClient) {
        super._init({
            title: 'Soft Dimmer',
            iconName: 'display-brightness-symbolic',
            toggleMode: true,
        });

        this._dbus = dbusClient;
        this._checked = false;

        // Initialize from D-Bus (asynchronous).
        this._dbus.getEnabled().then(enabled => {
            this._checked = enabled;
            this.checked = enabled;
        });

        // Connect to EnabledChanged signal.
        this._dbus.connectSignals({
            'EnabledChanged': (enabled) => {
                this._checked = enabled.get_boolean();
                this.checked = this._checked;
            }
        });
    }

    set checked(v) {
        this._checked = v;
        super.checked = v;
    }

    get checked() {
        return this._checked;
    }

    // QuickToggle calls this when the user clicks the toggle.
    _onToggleClicked() {
        const newState = !this._checked;
        this._dbus.setEnabled(newState).then(() => {
            // The EnabledChanged signal will update the UI.
        });
    }
});

// ── Slider row ────────────────────────────────────────────────────────────────

const SoftDimmerSlider = GObject.registerClass(
class SoftDimmerSlider extends St.BoxLayout {
    _init(dbusClient) {
        super._init({
            style_class: 'quick-slider',
            x_expand: true,
        });

        this._dbus = dbusClient;
        this._minBrightness = 0.10;
        this._ignoreSetting = false;

        // Dim icon on the left.
        this._iconMin = new St.Icon({
            icon_name: 'display-brightness-symbolic',
            style_class: 'quick-slider-icon',
            icon_size: 16,
        });
        this.add_child(this._iconMin);

        // Slider.
        this._slider = new Slider(1.0); // start at full brightness
        this._slider.x_expand = true;
        this.add_child(this._slider);

        // Bright icon on the right.
        this._iconMax = new St.Icon({
            icon_name: 'display-brightness-high-symbolic',
            style_class: 'quick-slider-icon',
            icon_size: 16,
        });
        this.add_child(this._iconMax);

        // Initialize from D-Bus.
        this._dbus.getMinimumBrightness().then(min => {
            this._minBrightness = min;
            return this._dbus.getBrightness(-1);
        }).then(brightness => {
            this._setSliderFromBrightness(brightness);
        });

        // Connect slider changes → D-Bus.
        this._slider.connect('notify::value', () => {
            if (this._ignoreSetting)
                return;
            const range = 1.0 - this._minBrightness;
            const brightness = this._minBrightness +
                this._slider.value * range;
            this._dbus.setBrightness(-1, brightness);
        });

        // Connect to BrightnessChanged signal.
        this._dbus.connectSignals({
            'BrightnessChanged': (variant) => {
                const [index, brightness] = [
                    variant.get_child_value(0).get_int32(),
                    variant.get_child_value(1).get_double(),
                ];
                if (index === -1) {
                    this._ignoreSetting = true;
                    this._setSliderFromBrightness(brightness);
                    this._ignoreSetting = false;
                }
            }
        });
    }

    _setSliderFromBrightness(brightness) {
        const range = 1.0 - this._minBrightness;
        const sliderValue = range > 0
            ? (brightness - this._minBrightness) / range
            : 1.0;
        this._slider.value = Math.max(0, Math.min(1, sliderValue));
    }

    destroy() {
        super.destroy();
    }
});

// ── System indicator ──────────────────────────────────────────────────────────

export const SoftDimmerIndicator = GObject.registerClass(
class SoftDimmerIndicator extends SystemIndicator {
    _init(dbusClient) {
        super._init();

        this._dbus = dbusClient;

        // Status icon shown in the panel.
        this._indicator = this._addIndicator();
        this._indicator.icon_name = 'display-brightness-symbolic';
        this._indicator.visible = false;

        // Build the Quick Settings menu.
        this._toggle = new SoftDimmerToggle(dbusClient);
        this._slider = new SoftDimmerSlider(dbusClient);

        this.quickSettingsItems.push(this._toggle);
        this.quickSettingsItems.push(this._slider);

        // Update visibility when brightness/enabled changes.
        this._dbus.connectSignals({
            'EnabledChanged': () => this._updateIndicatorVisibility(),
            'BrightnessChanged': () => this._updateIndicatorVisibility(),
        });

        this._updateIndicatorVisibility();
    }

    _updateIndicatorVisibility() {
        // Read from D-Bus asynchronously.
        Promise.all([
            this._dbus.getEnabled(),
            this._dbus.getBrightness(-1),
        ]).then(([enabled, brightness]) => {
            this._indicator.visible = enabled && brightness < 0.99;
        });
    }

    destroy() {
        super.destroy();
    }
});
