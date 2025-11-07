package process

import (
    "errors"
    "unsafe"

    "golang.org/x/sys/windows"
)

const (
	SE_DEBUG_NAME              = "SeDebugPrivilege"
	PROCESS_QUERY_INFORMATION  = 0x0400
	PROCESS_QUERY_LIMITED_INFO = 0x1000
	PROCESS_VM_READ            = 0x0010
	PROCESS_TERMINATE          = 0x0001
	PROCESS_SUSPEND_RESUME     = 0x0800
)

var (
	Psapi                     = windows.NewLazySystemDLL("psapi.dll")
	Advapi32                  = windows.NewLazySystemDLL("advapi32.dll")
	Ntdll                     = windows.NewLazySystemDLL("ntdll.dll")
	ProcGetModuleFileNameExW  = Psapi.NewProc("GetModuleFileNameExW")
	ProcLookupPrivilegeValueW = Advapi32.NewProc("LookupPrivilegeValueW")
	ProcAdjustTokenPrivileges = Advapi32.NewProc("AdjustTokenPrivileges")
	procNtSuspendProcess      = Ntdll.NewProc("NtSuspendProcess")
	procNtResumeProcess       = Ntdll.NewProc("NtResumeProcess")
)

// GetFilePathFromPID retrieves the executable file path of a given process ID.
func GetFilePathFromPID(pid uint32) (string, error) {
	token, err := windows.OpenCurrentProcessToken()
	if err == nil {
		defer token.Close()

		privName, _ := windows.UTF16PtrFromString(SE_DEBUG_NAME)
		var luid windows.LUID
		ret, _, _ := ProcLookupPrivilegeValueW.Call(0, uintptr(unsafe.Pointer(privName)), uintptr(unsafe.Pointer(&luid)))
		if ret != 0 {
			type tokenPrivileges struct {
				PrivilegeCount uint32
				Privileges     [1]struct {
					Luid       windows.LUID
					Attributes uint32
				}
			}
			tp := tokenPrivileges{
				PrivilegeCount: 1,
				Privileges: [1]struct {
					Luid       windows.LUID
					Attributes uint32
				}{{Luid: luid, Attributes: windows.SE_PRIVILEGE_ENABLED}},
			}
			ProcAdjustTokenPrivileges.Call(uintptr(token), 0, uintptr(unsafe.Pointer(&tp)), 0, 0, 0)
		}
	}

	hProcess, err := windows.OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, false, pid)
	if err != nil {
		hProcess, err = windows.OpenProcess(PROCESS_QUERY_LIMITED_INFO, false, pid)
		if err != nil {
			return "", err
		}
	}
	defer windows.CloseHandle(hProcess)

	buf := make([]uint16, windows.MAX_PATH)
	ret, _, err := ProcGetModuleFileNameExW.Call(uintptr(hProcess), 0, uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)))
	if ret == 0 {
		if err != nil && err.Error() == "The operation completed successfully." {
			if windows.UTF16ToString(buf) != "" {
				return windows.UTF16ToString(buf), nil
			}
		}
		return "", err
	}
	return windows.UTF16ToString(buf), nil
}

// TerminateByPID attempts to terminate the process identified by the provided PID.
func TerminateByPID(pid uint32) error {
    if pid == 0 {
        return errors.New("invalid process id")
    }

    handle, err := windows.OpenProcess(PROCESS_TERMINATE, false, pid)
    if err != nil {
        return err
    }
    defer windows.CloseHandle(handle)

    return windows.TerminateProcess(handle, 1)
}

// SuspendByPID suspends the process with the given PID.
func SuspendByPID(pid uint32) error {
    if pid == 0 {
        return errors.New("invalid process id")
    }

    handle, err := windows.OpenProcess(PROCESS_SUSPEND_RESUME, false, pid)
    if err != nil {
        return err
    }
    defer windows.CloseHandle(handle)

    r0, _, e1 := procNtSuspendProcess.Call(uintptr(handle))
    if r0 != 0 {
        if e1 != windows.ERROR_SUCCESS {
            return e1
        }
        return errors.New("NtSuspendProcess failed")
    }
    return nil
}

// ResumeByPID resumes the process with the given PID.
func ResumeByPID(pid uint32) error {
    if pid == 0 {
        return errors.New("invalid process id")
    }

    handle, err := windows.OpenProcess(PROCESS_SUSPEND_RESUME, false, pid)
    if err != nil {
        return err
    }
    defer windows.CloseHandle(handle)

    r0, _, e1 := procNtResumeProcess.Call(uintptr(handle))
    if r0 != 0 {
        if e1 != windows.ERROR_SUCCESS {
            return e1
        }
        return errors.New("NtResumeProcess failed")
    }
    return nil
}
