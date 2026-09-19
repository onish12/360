using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace PhaserM051 {
    // Device-instance scoped SetupAPI calls. Never removes a DriverStore package.
    public static class DeviceBinding {
        public const string HardwareId = @"PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06";
        public sealed class InstallResult {
            public bool Success;
            public bool RebootRequired;
            public int Win32Error;
        }
        [StructLayout(LayoutKind.Sequential)]
        public struct DeviceInfo {
            public uint Size;
            public Guid ClassGuid;
            public uint DevInst;
            public UIntPtr Reserved;
        }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        public struct InstallParams {
            public uint Size, Flags, FlagsEx;
            public IntPtr Parent, Callback, CallbackContext, FileQueue;
            public UIntPtr ClassReserved;
            public uint Reserved;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string DriverPath;
        }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        public struct DriverInfo {
            public uint Size, Type;
            public UIntPtr Reserved;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string Description;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string Manufacturer;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string Provider;
            public System.Runtime.InteropServices.ComTypes.FILETIME Date;
            public ulong Version;
        }
        [DllImport("setupapi.dll", SetLastError=true)]
        static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr guid, IntPtr parent);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiOpenDeviceInfoW(IntPtr set, string id, IntPtr parent, uint flags, ref DeviceInfo dev);
        [DllImport("setupapi.dll", SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr set, ref DeviceInfo dev, uint property,
            out uint type, [Out] byte[] data, uint capacity, out uint needed);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiGetDeviceInstallParamsW(IntPtr set, ref DeviceInfo dev, ref InstallParams p);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiSetDeviceInstallParamsW(IntPtr set, ref DeviceInfo dev, ref InstallParams p);
        [DllImport("setupapi.dll", SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiBuildDriverInfoList(IntPtr set, ref DeviceInfo dev, uint type);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiEnumDriverInfoW(IntPtr set, ref DeviceInfo dev, uint type, uint index, ref DriverInfo driver);
        [DllImport("setupapi.dll", SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetupDiDestroyDriverInfoList(IntPtr set, ref DeviceInfo dev, uint type);
        [DllImport("newdev.dll", ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool DiInstallDevice(IntPtr parent, IntPtr set, ref DeviceInfo dev,
            IntPtr driver, uint flags, [MarshalAs(UnmanagedType.Bool)] out bool reboot);

        public static void CheckAbi() {
            if (IntPtr.Size != 8 || Marshal.SizeOf(typeof(DeviceInfo)) != 32 ||
                Marshal.SizeOf(typeof(InstallParams)) != 584 || Marshal.SizeOf(typeof(DriverInfo)) != 1568)
                throw new InvalidOperationException("SETUPAPI_X64_ABI_MISMATCH");
        }
        static void Checked(bool value) {
            if (!value) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        static IntPtr Open(string instance, out DeviceInfo dev) {
            CheckAbi();
            if (instance == null || !instance.StartsWith(HardwareId + @"\", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("EXACT_INSTANCE_REQUIRED");
            IntPtr set = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
            if (set == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
            dev = new DeviceInfo { Size=32 };
            try {
                Checked(SetupDiOpenDeviceInfoW(set, instance, IntPtr.Zero, 0, ref dev));
                uint type, needed;
                var buffer = new byte[65536];
                Checked(SetupDiGetDeviceRegistryPropertyW(set, ref dev, 1, out type, buffer, (uint)buffer.Length, out needed));
                if (type != 7 || needed < 4 || needed > buffer.Length || needed % 2 != 0)
                    throw new InvalidOperationException("INVALID_HARDWARE_IDS");
                bool found = false;
                foreach (string id in Encoding.Unicode.GetString(buffer, 0, (int)needed).Split('\0'))
                    if (String.Equals(id, HardwareId, StringComparison.OrdinalIgnoreCase)) found = true;
                if (!found) throw new InvalidOperationException("EXACT_HARDWARE_ID_REQUIRED");
                return set;
            } catch { SetupDiDestroyDeviceInfoList(set); throw; }
        }
        // Null binding is a deliberate state change, not restoration of Intel.
        public static InstallResult InstallNull(string instance) {
            DeviceInfo dev;
            IntPtr set = Open(instance, out dev);
            try {
                bool reboot;
                bool ok=DiInstallDevice(IntPtr.Zero, set, ref dev, IntPtr.Zero, 4, out reboot);
                int error=ok ? 0 : Marshal.GetLastWin32Error();
                return new InstallResult { Success=ok, RebootRequired=reboot, Win32Error=error };
            } finally { SetupDiDestroyDeviceInfoList(set); }
        }
        public static InstallResult BindProbe(string instance, string storeInf) {
            string path = Path.GetFullPath(storeInf);
            string prefix = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                @"System32\DriverStore\FileRepository\");
            if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || path.Length >= 260 ||
                !String.Equals(Path.GetFileName(path), "phaser360_m051_mmio_ro.inf", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("OWNED_STORE_INF_REQUIRED");
            DeviceInfo dev;
            IntPtr set = Open(instance, out dev);
            bool built = false;
            IntPtr selected = IntPtr.Zero;
            try {
                var p = new InstallParams { Size=584 };
                Checked(SetupDiGetDeviceInstallParamsW(set, ref dev, ref p));
                p.Flags |= 0x00010000; // DI_ENUMSINGLEINF: only the verified, staged probe INF.
                p.DriverPath = path;
                Checked(SetupDiSetDeviceInstallParamsW(set, ref dev, ref p));
                Checked(SetupDiBuildDriverInfoList(set, ref dev, 2)); // SPDIT_COMPATDRIVER
                built = true;
                var first = new DriverInfo { Size=1568 };
                Checked(SetupDiEnumDriverInfoW(set, ref dev, 2, 0, ref first));
                var extra = new DriverInfo { Size=1568 };
                if (SetupDiEnumDriverInfoW(set, ref dev, 2, 1, ref extra))
                    throw new InvalidOperationException("AMBIGUOUS_PROBE_MODEL");
                if (Marshal.GetLastWin32Error() != 259) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (first.Provider != "PHASER360 Open Audio" || first.Version != 0x0000000500010000UL)
                    throw new InvalidOperationException("UNEXPECTED_PROBE_MODEL");
                selected = Marshal.AllocHGlobal(1568);
                Marshal.StructureToPtr(first, selected, false);
                bool reboot;
                bool ok=DiInstallDevice(IntPtr.Zero, set, ref dev, selected, 0, out reboot);
                int error=ok ? 0 : Marshal.GetLastWin32Error();
                return new InstallResult { Success=ok, RebootRequired=reboot, Win32Error=error };
            } finally {
                if (selected != IntPtr.Zero) Marshal.FreeHGlobal(selected);
                if (built) SetupDiDestroyDriverInfoList(set, ref dev, 2);
                SetupDiDestroyDeviceInfoList(set);
            }
        }
    }
}
