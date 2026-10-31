"""Import selected keyboard bindings without recording profile paths."""

import argparse
import json
from pathlib import Path
import tomllib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
FIELDS = {
    'Test': 'TEST', 'Service': 'SERVICE', 'Coin': 'COIN_ADD',
    'Wheel Axis Left': 'WHEEL_LEFT', 'Wheel Axis Right': 'WHEEL_RIGHT',
    'Gas': 'GAS', 'Brake': 'BRAKE', 'Enter Switch': 'TEST_ENTER',
    'Test Menu Up': 'TEST_UP', 'Test Menu Down': 'TEST_DOWN',
    'Gear Shift Up': 'GEAR_NEXT', 'Gear Shift Down': 'GEAR_PREVIOUS',
    'Perspective Switch Button': 'PERSPECTIVE', 'Interuption Switch Button': 'INTRUDE',
    **{f'Gear Shift {i}': f'GEAR_{name}' for i, name in enumerate(
        ['FIRST', 'SECOND', 'THIRD', 'FOURTH', 'FIFTH', 'SIXTH'], 1)},
}
SPECIAL = {'LeftAlt': 'LALT', 'LeftShift': 'LSHIFT', 'LeftControl': 'LCONTROL',
           'RightAlt': 'RALT', 'RightShift': 'RSHIFT', 'RightControl': 'RCONTROL',
           'Return': 'ENTER', 'Space': 'SPACE', 'Up': 'UPARROW', 'Down': 'DOWNARROW',
           'Left': 'LEFTARROW', 'Right': 'RIGHTARROW'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--game-exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/keyconfig.toml')
    options = parser.parse_args()
    try:
        profile = ET.parse(options.profile).getroot()
        if Path(profile.findtext('GamePath', '')).resolve() != options.game_exe.resolve():
            raise ValueError('profile mismatch')
        bindings = tomllib.loads((ROOT / 'dist/keyconfig.toml').read_text(encoding='utf-8'))
        unmapped = 0
        for button in profile.findall('./JoystickButtons/JoystickButtons'):
            name = button.findtext('ButtonName')
            if name not in FIELDS:
                continue
            original = button.findtext('BindNameDi', '')
            if not original.startswith('Keyboard Button '):
                continue
            key = original.removeprefix('Keyboard Button ')
            if key in SPECIAL:
                translated = SPECIAL[key]
            elif len(key) == 2 and key[0] == 'D' and key[1].isdigit():
                translated = 'NUM' + key[1]
            elif (len(key) == 1 and key.isalpha()) or (key.startswith('F') and key[1:].isdigit()):
                translated = key.upper()
            else:
                unmapped += 1
                continue
            destination = FIELDS[name]
            bindings[destination] = [translated] + [x for x in bindings[destination] if x.startswith('SDL_')]
        bindings['START'] = list(dict.fromkeys(bindings['TEST_ENTER'] + ['ENTER', 'SDL_START']))
        bindings['CARD_INSERT'] = ['P']
        text = '# Imported keyboard bindings. Source profile is not recorded.\n'
        text += '# Legacy unnamed test-menu keys 105/110 use arrow up/down.\n\n'
        text += '\n'.join(f'{key} = {json.dumps(values)}' for key, values in bindings.items()) + '\n'
        options.output.parent.mkdir(parents=True, exist_ok=True)
        options.output.write_text(text, encoding='utf-8')
        print(json.dumps({'binding_count': len(bindings), 'unmapped_count': unmapped}))
    except (OSError, ValueError, ET.ParseError):
        parser.exit(1, 'Import failed: check the selected profile, game executable and output locally.\n')


if __name__ == '__main__':
    main()
