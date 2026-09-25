# H15OR — read-only recovery probe after H15O runner failure

H15O live execution on 2026-09-23 returned a driver NTSTATUS whose raw bytes
represent 0xC0000182. The PowerShell runner then failed while converting that
signed Int32 directly to UInt32, before persisting the 464-byte driver result
or computing its restore proof. The conservative finally path correctly kept
H15O active and retained its trust.

H15OR does not bind to DEV_3198 and does not modify H15O. It is a temporary,
test-signed legacy control driver used only to read:
- PCI config PGCTL 0x44 and CGCTL 0x48 through HalGetBusDataByOffset;
- verified physical HDA BAR 0xCEEE0000 read-only;
- verified physical DSP BAR 0xCEF00000 read-only.

The runner obtains PCI bus/device/function from read-only PnP properties.
Microsoft documents PCI DevicePropertyAddress as device number in the high word
and function number in the low word.

H15O is uninstalled only when all owned writable state is exact:
PGCTL=0x00000010, CGCTL=0x807B0DFF, GCTL=0, EM2=0x04007000,
PPCTL=0, HDA transport idle, and stable DSP readback
ADSPCS=0x001D003C, HIPCI=0, HIPCIE=0x00420000, HIPCCTL=0,
ROM_STATUS=0x01006701. ADSPIC/ADSPIS are captured but are not used as
handoff gates because H15O never writes DSP BAR state.

If any gate is not exact, H15O remains installed and trusted.
