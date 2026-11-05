// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus client for the ASUSCraft Soft Dimmer engine.
// Connects to io.github.asuscraft.SoftDimmer on the session bus.
// Falls back gracefully when ASUSCraft is not running.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

const DBUS_SERVICE = 'io.github.asuscraft.SoftDimmer';
const DBUS_OBJECT = '/io/github/asuscraft/SoftDimmer';
const DBUS_IFACE = 'io.github.asuscraft.SoftDimmer';

const DBUS_INTROSPECTION = `
<node>
  <interface name="${DBUS_IFACE}">
    <method name="GetBrightness">
      <arg name="monitor_index" type="i" direction="in"/>
      <arg name="brightness" type="d" direction="out"/>
    </method>
    <method name="SetBrightness">
      <arg name="monitor_index" type="i" direction="in"/>
      <arg name="brightness" type="d" direction="in"/>
    </method>
    <method name="GetEnabled">
      <arg name="enabled" type="b" direction="out"/>
    </method>
    <method name="SetEnabled">
      <arg name="enabled" type="b" direction="in"/>
    </method>
    <method name="GetMinimumBrightness">
      <arg name="min" type="d" direction="out"/>
    </method>
    <method name="SetMinimumBrightness">
      <arg name="min" type="d" direction="in"/>
    </method>
    <method name="GetTransitionMs">
      <arg name="ms" type="i" direction="out"/>
    </method>
    <method name="SetTransitionMs">
      <arg name="ms" type="i" direction="in"/>
    </method>
    <method name="GetStatus">
      <arg name="status" type="s" direction="out"/>
      <arg name="enabled" type="b" direction="out"/>
      <arg name="brightness" type="d" direction="out"/>
      <arg name="monitor_count" type="i" direction="out"/>
    </method>
    <signal name="BrightnessChanged">
      <arg name="monitor_index" type="i"/>
      <arg name="brightness" type="d"/>
    </signal>
    <signal name="EnabledChanged">
      <arg name="enabled" type="b"/>
    </signal>
    <signal name="StatusChanged">
      <arg name="status" type="s"/>
    </signal>
  </interface>
</node>
`;

/**
 * SoftDimmerDbusClient — asynchronous D-Bus client to the ASUSCraft engine.
 *
 * All methods return GLib.Variant tuples. Signals are connected via
 * Gio.DBusProxy signal handlers. The client never blocks the GNOME Shell
 * main thread: all calls are asynchronous with callbacks.
 */
export class SoftDimmerDbusClient {
    constructor() {
        this._proxy = null;
        this._connection = null;
        this._available = false;
        this._signalIds = [];
    }

    /** Try to acquire a proxy to the ASUSCraft service. Returns TRUE if
     *  the service is available on the bus. Asynchronous. */
    async connect() {
        try {
            this._connection = await Gio.bus_get_async(Gio.BusType.SESSION);
            this._proxy = await Gio.DBusProxy.new_async(
                this._connection,
                Gio.DBusProxyFlags.NONE,
                null, // no GDBusNodeInfo: we use the well-known interface
                DBUS_SERVICE,
                DBUS_OBJECT,
                DBUS_IFACE,
                null
            );
            this._available = true;
            return true;
        } catch (e) {
            g_printerr(`soft-dimmer-asc: ASUSCraft D-Bus service unavailable: ${e}`);
            this._available = false;
            this._proxy = null;
            this._connection = null;
            return false;
        }
    }

    get available() {
        return this._available && this._proxy !== null;
    }

    /** Call GetBrightness(monitor_index) → brightness. */
    async getBrightness(monitorIndex = -1) {
        if (!this.available)
            return 1.0;
        try {
            const [result] = await this._proxy.call_async('GetBrightness',
                GLib.Variant.new('(i)', [monitorIndex]),
                Gio.DBusCallFlags.NONE, -1, null);
            return result.get_child_value(0).get_double();
        } catch (e) {
            g_printerr(`soft-dimmer-asc: GetBrightness failed: ${e}`);
            return 1.0;
        }
    }

    /** Call SetBrightness(monitor_index, brightness). */
    async setBrightness(monitorIndex, brightness) {
        if (!this.available)
            return false;
        try {
            await this._proxy.call_async('SetBrightness',
                GLib.Variant.new('(id)', [monitorIndex, brightness]),
                Gio.DBusCallFlags.NONE, -1, null);
            return true;
        } catch (e) {
            g_printerr(`soft-dimmer-asc: SetBrightness failed: ${e}`);
            return false;
        }
    }

    /** Call GetEnabled() → enabled. */
    async getEnabled() {
        if (!this.available)
            return false;
        try {
            const [result] = await this._proxy.call_async('GetEnabled',
                GLib.Variant.new('()'),
                Gio.DBusCallFlags.NONE, -1, null);
            return result.get_child_value(0).get_boolean();
        } catch (e) {
            g_printerr(`soft-dimmer-asc: GetEnabled failed: ${e}`);
            return false;
        }
    }

    /** Call SetEnabled(enabled). */
    async setEnabled(enabled) {
        if (!this.available)
            return false;
        try {
            await this._proxy.call_async('SetEnabled',
                GLib.Variant.new('(b)', [enabled]),
                Gio.DBusCallFlags.NONE, -1, null);
            return true;
        } catch (e) {
            g_printerr(`soft-dimmer-asc: SetEnabled failed: ${e}`);
            return false;
        }
    }

    /** Connect to D-Bus signals. handlerMap keys: 'BrightnessChanged',
     *  'EnabledChanged', 'StatusChanged'. Values: callback functions. */
    connectSignals(handlerMap) {
        if (!this._proxy)
            return;
        for (const [signalName, handler] of Object.entries(handlerMap)) {
            const id = this._proxy.connect(signalName, (_proxy, value) => {
                handler(value);
            });
            this._signalIds.push(id);
        }
    }

    /** Disconnect all signal handlers. */
    disconnectSignals() {
        if (!this._proxy)
            return;
        for (const id of this._signalIds)
            this._proxy.disconnect(id);
        this._signalIds = [];
    }

    /** Close the connection. */
    close() {
        this.disconnectSignals();
        this._proxy = null;
        this._connection = null;
        this._available = false;
    }
}
