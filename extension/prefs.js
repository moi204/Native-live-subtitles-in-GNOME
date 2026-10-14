import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk';
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

const LANGUAGES = [
    'auto', 'en-US', 'en-GB', 'es-US', 'es-ES', 'fr-FR', 'fr-CA',
    'it-IT', 'pt-BR', 'pt-PT', 'nl-NL', 'de-DE', 'tr-TR', 'ru-RU',
    'ar-AR', 'hi-IN', 'ja-JP', 'ko-KR', 'vi-VN', 'uk-UA', 'pl-PL',
    'sv-SE', 'cs-CZ', 'nb-NO', 'da-DK', 'bg-BG', 'fi-FI', 'hr-HR',
    'sk-SK', 'zh-CN', 'hu-HU', 'ro-RO', 'et-EE', 'el-GR', 'lt-LT',
    'lv-LV', 'mt-MT', 'sl-SI', 'he-IL', 'th-TH', 'nn-NO',
];
const LANGUAGE_NAMES = [
    'Automatic', 'English (US)', 'English (UK)', 'Spanish (US)', 'Spanish (Spain)',
    'French (France)', 'French (Canada)', 'Italian', 'Portuguese (Brazil)',
    'Portuguese (Portugal)', 'Dutch', 'German', 'Turkish', 'Russian', 'Arabic',
    'Hindi', 'Japanese', 'Korean', 'Vietnamese', 'Ukrainian', 'Polish', 'Swedish',
    'Czech', 'Norwegian Bokmål', 'Danish', 'Bulgarian', 'Finnish', 'Croatian',
    'Slovak', 'Mandarin', 'Hungarian', 'Romanian', 'Estonian', 'Greek', 'Lithuanian',
    'Latvian', 'Maltese', 'Slovenian', 'Hebrew', 'Thai', 'Norwegian Nynorsk',
];

export default class LiveCaptionsPreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage();
        const group = new Adw.PreferencesGroup({
            title: 'Live Captions',
            description: 'Parakeet transcription',
        });
        const row = new Adw.ActionRow({
            title: 'Language',
            subtitle: 'Language used by the live transcription model',
        });
        const model = Gtk.StringList.new(LANGUAGE_NAMES);
        const dropdown = new Gtk.DropDown({model});
        dropdown.set_valign(Gtk.Align.CENTER);
        row.add_suffix(dropdown);
        row.set_activatable_widget(dropdown);
        group.add(row);
        page.add(group);
        window.add(page);

        const sync = () => {
            const index = LANGUAGES.indexOf(settings.get_string('language'));
            dropdown.set_selected(index >= 0 ? index : 0);
        };
        sync();
        dropdown.connect('notify::selected', () => {
            const index = dropdown.get_selected();
            if (index < LANGUAGES.length)
                settings.set_string('language', LANGUAGES[index]);
        });
        settings.connect('changed::language', sync);
    }
}
