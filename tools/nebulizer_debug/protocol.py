"""Wire snapshot decoder. Independent of Qt, pyserial and C struct layouts."""
import struct

MAGIC = 0x3144424E
VERSION = 1
MAILBOX_SIZE = 928
ACQUIRE, RELEASE = 0xE0, 0xE1
COMMANDS = {
    0x10: '模式', 0x11: '目标温度', 0x12: '治疗时间', 0x13: '风量',
    0x14: '雾量', 0x15: '保温', 0x20: '开始', 0x21: '暂停',
    0x22: '恢复', 0x23: '停止治疗', 0x24: '读取状态', 0x25: '心跳',
    0x26: '清除故障', 0x27: '读取配置', 0x28: '读取出口 PID', 0x29: '设置出口 PID',
    0x2A: '读取运行状态', 0x2B: '进入维护', 0x2C: '退出维护',
    0x2D: '手动风量', 0x2E: '手动雾量', 0x2F: '手动加热', 0x30: '读取维护状态',
    0x31: '读取风机 PID', 0x32: '设置风机 PID', 0x33: '读取旧预热 PID',
    0x34: '设置旧预热 PID', ACQUIRE: '接管调试控制', RELEASE: '释放调试控制',
}
STATES = ['启动', '自检', '就绪', '配置中', '热雾治疗', '冷雾治疗', '已暂停',
          '治疗完成', '故障', '预热中', '保温中']
EVENTS = {1: '预热开始', 2: '正式治疗开始', 3: '治疗完成', 4: '保温开始', 5: '保温结束'}
ERRORS = {0x40: '当前状态拒绝操作', 0x41: 'OTP 复位失败', 0x42: '盖子未关闭',
          0x43: '雾化板缺水', 0x44: '雾化板离线', 0x45: '雾化板安全锁定',
          0x70: '控制权不属于此端（旧固件也可能拒绝运行中接管）', 0x71: '调试请求格式错误',
          0x72: '调试会话已失效', 0x7E: '队列繁忙', 0x7F: '不支持的命令'}

def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc

def decode_frames(data):
    frames = {}
    while data:
        if len(data) < 11 or data[:2] != b'\xaa\x55':
            raise ValueError('快照帧头错误')
        frame_id, kind, length = struct.unpack_from('<HBH', data, 2)
        end = length + 11
        if length > 128 or len(data) < end or data[end-2:end] != b'\r\n':
            raise ValueError('快照帧长度错误')
        if crc16(data[2:7+length]) != struct.unpack_from('<H', data, 7+length)[0]:
            raise ValueError('快照 CRC 错误')
        if kind in frames:
            raise ValueError('重复的快照帧')
        frames[kind] = data[7:7+length]
        data = data[end:]
    return frames

def decode_snapshot(data):
    f = decode_frames(data)
    for kind, size in {4:21, 6:8, 7:20, 8:92, 9:5, 10:20, 11:20}.items():
        if len(f.get(kind, b'')) != size:
            raise ValueError(f'缺少或不兼容的快照帧 {kind}')
    config = dict(zip(('mode','target_temp_deci_c','duration_sec','air_level','mist_level','keep_warm_enabled'),
                      struct.unpack('<BHHBBB', f[6])))
    pids = {name: dict(zip(('kp_milli','ki_milli','kd_milli','integral_limit_permille'),
                           struct.unpack_from('<4i', f[k])))
            for k, name in ((7,'heat'),(10,'fan'),(11,'preheat'))}
    p = f[8]
    def read(fmt, at): return struct.unpack_from('<'+fmt, p, at)[0]
    runtime = {name: read(fmt, off) for name, fmt, off in (
        ('state','B',0),('remaining_sec','I',1),('fault','H',5),('heartbeat','B',7),
        ('liquid','B',16),('cover','B',17),('gx1832','B',18),('fan_rpm','H',19),
        ('mist_online','B',21),('mist_running','B',22),('mist_desired','B',23),
        ('mist_actual','B',24),('mist_fault','H',25),('heat_enabled','B',27),
        ('heat_output','H',30),('heat_error','h',32),('heat_measured','h',34),
        ('heat_target','h',36),('sample_ms','I',38),('heat_integral','i',62),
        ('adc_max','H',74),('fan_pid_enabled','B',76),('fan_saturated','B',77),
        ('fan_base','B',78),('fan_output','B',79),('fan_boost','H',80),
        ('fan_error','h',82),('fan_measured','h',84),('fan_target','h',86),
        ('fan_integral','h',88),('phase','B',90))}
    runtime['temperatures'] = list(struct.unpack_from('<4h', p, 8))
    runtime['adc'] = list(struct.unpack_from('<4H', p, 66))
    maintenance = dict(zip(('active','fan','mist','heat'), struct.unpack('<BBBH', f[9])))
    return {'config':config,'pids':pids,'runtime':runtime,'maintenance':maintenance,
            'threshold':struct.unpack_from('<H', f[11],16)[0],
            'full_power':struct.unpack_from('<H', f[11],18)[0]}

def validate_params(p):
    """Validate the entire legacy JSON file before changing any UI or device."""
    limits = {'mode':(0,1), 'target_temp_deci_c':(0,450), 'duration_min':(1,60),
              'air_level':(0,3), 'mist_level':(0,3), 'i_limit_permille':(0,1000),
              'preheat_i_limit_permille':(0,1000), 'fan_i_limit_permille':(0,100)}
    for key, (lo, hi) in limits.items():
        if type(p.get(key)) is not int or not lo <= p[key] <= hi:
            raise ValueError(f'{key} 必须为 {lo}..{hi} 的整数')
    import math
    for prefix, maximum in (('',(120,10,5)),('preheat_',(120,10,5)),('fan_',(20,5,5))):
        for key, hi in zip(('kp','ki','kd'), maximum):
            v = p.get(prefix+key)
            if type(v) not in (int,float) or not math.isfinite(v) or not 0 <= v <= hi:
                raise ValueError(f'{prefix+key} 必须在 0..{hi} 范围内')
    if type(p.get('keep_warm_enabled')) is not bool:
        raise ValueError('keep_warm_enabled 必须为布尔值')
    if p['mode'] == 1 and p['keep_warm_enabled']:
        raise ValueError('冷雾配置不能启用保温')
    return p
