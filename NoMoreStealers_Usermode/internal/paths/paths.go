package paths

import (
	"golang.org/x/sys/windows"
)

func DevicePathToDOSPath(devicePath string) (string, bool) {
	if len(devicePath) < 8 {
		return "", false
	}

	if devicePath[:8] != "\\Device\\" {
		return "", false
	}

	devicePathUTF16, err := windows.UTF16FromString(devicePath)
	if err != nil {
		return "", false
	}

	driveBuf := make([]uint16, 256)
	result, err := windows.GetLogicalDriveStrings(uint32(len(driveBuf)-1), &driveBuf[0])
	if err != nil || result == 0 || result >= uint32(len(driveBuf)) {
		return "", false
	}

	for i := 0; i < int(result); {
		if i+2 >= int(result) || driveBuf[i] == 0 {
			break
		}

		driveLetterUTF16 := driveBuf[i : i+4]
		if driveLetterUTF16[2] != uint16('\\') || driveLetterUTF16[3] != 0 {
			for i < int(result) && driveBuf[i] != 0 {
				i++
			}
			if i < int(result) {
				i++
			}
			continue
		}

		devicePathBuf := make([]uint16, windows.MAX_PATH)
		retLen, err := windows.QueryDosDevice(&driveLetterUTF16[0], &devicePathBuf[0], uint32(len(devicePathBuf)))
		if err == nil && retLen > 0 {
			var devicePathLen int
			for j := 0; j < len(devicePathBuf); j++ {
				if devicePathBuf[j] == 0 {
					devicePathLen = j
					break
				}
			}

			if len(devicePathUTF16)-1 >= devicePathLen {
				match := true
				for j := 0; j < devicePathLen; j++ {
					if devicePathUTF16[j] != devicePathBuf[j] {
						match = false
						break
					}
				}

				if match {
					restOfPathUTF16 := devicePathUTF16[devicePathLen:]
					driveLetterStr := windows.UTF16ToString(driveLetterUTF16[:3])
					restOfPathStr := windows.UTF16ToString(restOfPathUTF16)
					dosPath := driveLetterStr[:2] + restOfPathStr
					return dosPath, true
				}
			}
		}

		for i < int(result) && driveBuf[i] != 0 {
			i++
		}
		if i < int(result) {
			i++
		}
	}

	return "", false
}
