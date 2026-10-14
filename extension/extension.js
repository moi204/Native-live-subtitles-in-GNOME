import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as QuickSettings from 'resource:///org/gnome/shell/ui/quickSettings.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS = 'net.sapples.LiveCaptions';
const PATH = '/net/sapples/LiveCaptions/CaptionService';
const IFACE = 'net.sapples.LiveCaptions';

const LiveCaptionsToggle = GObject.registerClass(
class LiveCaptionsToggle extends QuickSettings.QuickToggle {
    _init(settings) {
        super._init({
            title: 'Live Captions',
            subtitle: 'Parakeet',
            iconName: 'media-view-subtitles-symbolic',
            toggleMode: true,
        });
        this._settings = settings;
        this._proxy = null;
        this._destroyed = false;
        this._requestedActive = null;
        this._syncing = false;
        this._retryId = 0;
        this._settingsChangedId = this._settings.connect('changed::language', () => this._sendLanguage());
        this.connect('notify::checked', () => {
            if (this._syncing)
                return;
            this._requestedActive = this.checked;
            if (this.checked) this._sendLanguage();
            this._setActive();
        });
        Gio.DBusProxy.new_for_bus(
            Gio.BusType.SESSION,
            Gio.DBusProxyFlags.NONE,
            null, BUS, PATH, IFACE, null,
            (_source, result) => {
                if (this._destroyed)
                    return;
                try {
                    this._proxy = Gio.DBusProxy.new_for_bus_finish(result);
                    this._proxy.connect('g-signal', (_proxy, _sender, signal, params) => {
                        if (signal === 'ActiveChanged') {
                            const active = params.deep_unpack()[0];
                            if (this._requestedActive === null || this._requestedActive === active) {
                                this._requestedActive = null;
                                this._setChecked(active);
                            }
                        }
                        if (signal === 'LanguageChanged')
                            this._settings.set_string('language', params.deep_unpack()[0]);
                    });
                    this._proxy.connect('g-properties-changed', () => {
                        // During service activation the cached D-Bus property
                        // can briefly still be false. Do not overwrite the
                        // checked state while the requested activation is in
                        // progress, or while the service has just confirmed
                        // the window is active.
                        if (this._proxy.Active)
                            this._setChecked(true);
                        else if (this._requestedActive !== true && !this.checked)
                            this._setChecked(this._proxy.Active);
                    });
                    if (this._requestedActive === null)
                        this._setChecked(this._proxy.Active);
                    else
                        this._setActive();
                } catch (error) {
                    logError(error, 'Live Captions D-Bus service unavailable');
                }
            });
    }

    _sendLanguage() {
        if (!this._proxy)
            return;
        this._proxy.call('SetLanguage', new GLib.Variant('(s)', [this._settings.get_string('language')]),
            Gio.DBusCallFlags.NONE, -1, null, null);
    }

    _setChecked(value) {
        if (this.checked === value)
            return;
        this._syncing = true;
        this.checked = value;
        this._syncing = false;
    }

    _setActive() {
        if (!this._proxy || this._destroyed)
            return;
        const requested = this.checked;
        this._proxy.call('SetActive', new GLib.Variant('(b)', [requested]),
            Gio.DBusCallFlags.NONE, 5000, null, (proxy, result) => {
                try {
                    proxy.call_finish(result);
                } catch (error) {
                    // The first click can arrive while the user service is
                    // still being activated. Retry that same requested state
                    // instead of requiring a second click.
                    if (!this._destroyed && this.checked === requested && !this._retryId) {
                        this._retryId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 400, () => {
                            this._retryId = 0;
                            this._setActive();
                            return GLib.SOURCE_REMOVE;
                        });
                    }
                }
            });
    }

    destroy() {
        this._destroyed = true;
        if (this._settingsChangedId)
            this._settings.disconnect(this._settingsChangedId);
        if (this._retryId) {
            GLib.source_remove(this._retryId);
            this._retryId = 0;
        }
        this._proxy = null;
        super.destroy();
    }
});

export default class LiveCaptionsExtension extends Extension {
    enable() {
        this._settings = this.getSettings();
        this._indicator = new QuickSettings.SystemIndicator();
        this._toggle = new LiveCaptionsToggle(this._settings);
        this._indicator.quickSettingsItems.push(this._toggle);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        if (this._indicator) {
            // SystemIndicator does not own the QuickToggle objects pushed into
            // quickSettingsItems on every GNOME version. Destroy them first so
            // a reload cannot leave a stale duplicate pill in the menu.
            for (const item of this._indicator.quickSettingsItems)
                item.destroy();
            this._indicator.quickSettingsItems = [];
            this._indicator.destroy();
        }
        this._indicator = null;
        this._toggle = null;
        this._settings = null;
    }
}
