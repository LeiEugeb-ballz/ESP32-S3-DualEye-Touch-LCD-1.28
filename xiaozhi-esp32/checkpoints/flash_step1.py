import os, sys, subprocess

CHECKPOINT_BIN = os.path.join(os.path.dirname(__file__), 'step1_multi_eye_working.bin')
ESP_PYTHON = r'C:\Users\txr45\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'

def flash(port='COM13'):
    print(f'Flashing Step 1 Checkpoint to {port}...')
    cmd = [
        ESP_PYTHON, '-m', 'esptool',
        '--chip', 'esp32s3',
        '-p', port,
        '-b', '460800',
        '--before', 'default_reset',
        '--after', 'hard_reset',
        'write_flash',
        '--flash_mode', 'dio',
        '--flash_freq', '80m',
        '--flash_size', '16MB',
        '0x0', CHECKPOINT_BIN
    ]
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print('Checkpoint Flash Successful!')
    else:
        print('Flash failed.')

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM13'
    flash(port)
