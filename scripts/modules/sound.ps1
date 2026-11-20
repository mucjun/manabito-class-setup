# まなびと講習準備 ― 音の点検（既定の再生機器の音量と消音）

$script:AudioCode = @'
using System;
using System.Runtime.InteropServices;
namespace ManabitoAudio {
    [ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioEndpointVolume {
        int RegisterControlChangeNotify(IntPtr p);
        int UnregisterControlChangeNotify(IntPtr p);
        int GetChannelCount(out uint n);
        int SetMasterVolumeLevel(float level, ref Guid ctx);
        int SetMasterVolumeLevelScalar(float level, ref Guid ctx);
        int GetMasterVolumeLevel(out float level);
        int GetMasterVolumeLevelScalar(out float level);
        int SetChannelVolumeLevel(uint ch, float level, ref Guid ctx);
        int SetChannelVolumeLevelScalar(uint ch, float level, ref Guid ctx);
        int GetChannelVolumeLevel(uint ch, out float level);
        int GetChannelVolumeLevelScalar(uint ch, out float level);
        int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
        int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
    }
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumerator { }

    public static class Volume {
        static IAudioEndpointVolume Endpoint() {
            var e = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
            IMMDevice dev;
            Marshal.ThrowExceptionForHR(e.GetDefaultAudioEndpoint(0, 1, out dev)); // eRender, eMultimedia
            Guid iid = typeof(IAudioEndpointVolume).GUID;
            object o;
            Marshal.ThrowExceptionForHR(dev.Activate(ref iid, 23, IntPtr.Zero, out o)); // CLSCTX_ALL
            return (IAudioEndpointVolume)o;
        }
        public static float GetLevel() {
            float v;
            Marshal.ThrowExceptionForHR(Endpoint().GetMasterVolumeLevelScalar(out v));
            return v;
        }
        public static bool GetMute() {
            bool m;
            Marshal.ThrowExceptionForHR(Endpoint().GetMute(out m));
            return m;
        }
        public static void SetMute(bool mute) {
            Guid ctx = Guid.Empty;
            Marshal.ThrowExceptionForHR(Endpoint().SetMute(mute, ref ctx));
        }
    }
}
'@

# 音量の部品を用意する（同じ PowerShell の中で 2 回目以降は何もしない）
function Initialize-Audio {
    if (-not ('ManabitoAudio.Volume' -as [type])) {
        Add-Type -TypeDefinition $script:AudioCode -Language CSharp -ErrorAction Stop
    }
}

function Test-Volume {
    param($Prof)
    try {
        Initialize-Audio
    } catch {
        return New-Row 'volume' '音量' 'ERR' '' '消音' ('音量を調べる部品を用意できませんでした: ' + $_.Exception.Message)
    }
    try {
        $level = [ManabitoAudio.Volume]::GetLevel()
        $mute = [ManabitoAudio.Volume]::GetMute()
    } catch {
        $hr = $_.Exception.InnerException
        if ($null -eq $hr) { $hr = $_.Exception }
        # 0x80070490 = 要素が見つからない（再生機器が無い）
        if ($hr.HResult -eq -2147023728) {
            return New-Row 'volume' '音量' 'NA' '再生機器なし' '消音' 'スピーカーやイヤホンの出力先が見つかりません。'
        }
        return New-Row 'volume' '音量' 'ERR' '' '消音' ('音量を読めませんでした: ' + $hr.Message)
    }
    $pct = [int][Math]::Round($level * 100)
    if ($mute) { $cur = ('消音（音量 {0}%）' -f $pct) } else { $cur = ('音量 {0}%' -f $pct) }
    $state = 'NG'; if ($mute -or $pct -eq 0) { $state = 'OK' }
    return New-Row 'volume' '音量' $state $cur '消音' '既定の再生機器を見ています。'
}
