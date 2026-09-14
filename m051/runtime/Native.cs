using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace PhaserM051 {
    public static class Native {
        [StructLayout(LayoutKind.Sequential)]
        private struct CodeIntegrity { public uint Length; public uint Options; }
        [DllImport("ntdll.dll", ExactSpelling = true)]
        private static extern int NtQuerySystemInformation(int kind, ref CodeIntegrity data,
            uint length, out uint returned);
        [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern uint CM_Get_Device_Interface_List_SizeW(out uint size,
            ref Guid guid, string device, uint flags);
        [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern uint CM_Get_Device_Interface_ListW(ref Guid guid,
            string device, [Out] char[] buffer, uint length, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access,
            uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeviceIoControl(SafeFileHandle file, uint ioctl,
            IntPtr input, uint inputBytes, [Out] byte[] output, uint outputBytes,
            out uint returned, IntPtr overlapped);

        public static uint CodeIntegrityOptions() {
            var info = new CodeIntegrity { Length = 8 };
            uint returned;
            // SystemCodeIntegrityInformation in the Windows SDK winternl.h.
            int status = NtQuerySystemInformation(103, ref info, 8, out returned);
            if (status != 0 || returned != 8 || info.Length != 8)
                throw new InvalidOperationException("CODE_INTEGRITY_QUERY_FAILED: " + status);
            return info.Options;
        }

        public static byte[] ReadSnapshot(string instanceId) {
            Guid guid = new Guid("6E816B38-0149-4DC7-A3B3-79DF42DD6511");
            uint size;
            uint status = CM_Get_Device_Interface_List_SizeW(out size, ref guid, instanceId, 0);
            if (status != 0 || size < 2 || size > 32768)
                throw new InvalidOperationException("INTERFACE_SIZE_FAILED: " + status);
            var chars = new char[size];
            status = CM_Get_Device_Interface_ListW(ref guid, instanceId, chars, size, 0);
            if (status != 0) throw new InvalidOperationException("INTERFACE_LIST_FAILED: " + status);
            string[] paths = new string(chars).Split(new char[] {'\0'}, StringSplitOptions.RemoveEmptyEntries);
            if (paths.Length != 1) throw new InvalidOperationException("EXPECTED_ONE_TARGET_INTERFACE");
            using (SafeFileHandle file = CreateFileW(paths[0], 0x80000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
                if (file.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                var bytes = new byte[56];
                uint returned;
                if (!DeviceIoControl(file, 0x00226000, IntPtr.Zero, 0, bytes, 56, out returned, IntPtr.Zero))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                if (returned != 56) throw new InvalidOperationException("SNAPSHOT_LENGTH_MISMATCH");
                return bytes;
            }
        }
    }
}
