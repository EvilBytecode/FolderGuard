package tcp

import (
	"fmt"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	Iphlpapi                    = windows.NewLazySystemDLL("iphlpapi.dll")
	ProcGetExtendedTcpTable     = Iphlpapi.NewProc("GetExtendedTcpTable")
	ProcGetExtendedUdpTable     = Iphlpapi.NewProc("GetExtendedUdpTable")
)

const (
	TCP_TABLE_OWNER_PID_ALL = 5
	UDP_TABLE_OWNER_PID     = 1
	AF_INET                 = 2
	AF_INET6                = 23
)

type MIB_TCPROW_OWNER_PID struct {
	State      uint32
	LocalAddr  uint32
	LocalPort  uint32
	RemoteAddr uint32
	RemotePort uint32
	OwningPid  uint32
}

type MIB_TCP6ROW_OWNER_PID struct {
	LocalAddr     [16]byte
	LocalScopeId  uint32
	LocalPort     uint32
	RemoteAddr    [16]byte
	RemoteScopeId uint32
	RemotePort    uint32
	State         uint32
	OwningPid     uint32
}

type MIB_TCPTABLE_OWNER_PID struct {
	NumEntries uint32
	Table      [1]MIB_TCPROW_OWNER_PID
}

type MIB_TCP6TABLE_OWNER_PID struct {
	NumEntries uint32
	Table      [1]MIB_TCP6ROW_OWNER_PID
}

// TCPConnectionInfo holds information about a TCP connection
type TCPConnectionInfo struct {
	LocalAddr  string
	LocalPort  uint32
	RemoteAddr string
	RemotePort uint32
	State      uint32
}

// GetTCPConnections returns a map of PID to whether it has active TCP connections
func GetTCPConnections() (map[uint32]bool, error) {
	result := make(map[uint32]bool)
	
	// Get IPv4 TCP connections
	if err := getTCPConnectionsIPv4(result); err != nil {
		return nil, fmt.Errorf("failed to get IPv4 TCP connections: %w", err)
	}
	
	// Get IPv6 TCP connections
	if err := getTCPConnectionsIPv6(result); err != nil {
		// IPv6 might not be available, so we'll just log and continue
		// Don't return error for IPv6
	}
	
	return result, nil
}

// GetTCPConnectionsDetailed returns a map of PID to their TCP connection details
func GetTCPConnectionsDetailed() (map[uint32][]TCPConnectionInfo, error) {
	result := make(map[uint32][]TCPConnectionInfo)
	
	// Get IPv4 TCP connections
	if err := getTCPConnectionsIPv4Detailed(result); err != nil {
		return nil, fmt.Errorf("failed to get IPv4 TCP connections: %w", err)
	}
	
	// Get IPv6 TCP connections
	if err := getTCPConnectionsIPv6Detailed(result); err != nil {
		// IPv6 might not be available, so we'll just log and continue
	}
	
	return result, nil
}

func ipv4ToString(addr uint32) string {
	return fmt.Sprintf("%d.%d.%d.%d",
		byte(addr),
		byte(addr>>8),
		byte(addr>>16),
		byte(addr>>24))
}

func getTCPConnectionsIPv4Detailed(result map[uint32][]TCPConnectionInfo) error {
	var size uint32
	
	ret, _, _ := ProcGetExtendedTcpTable.Call(
		0,
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 122 {
		return fmt.Errorf("unexpected return value: %d", ret)
	}
	
	buf := make([]byte, size)
	ret, _, _ = ProcGetExtendedTcpTable.Call(
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 0 {
		return fmt.Errorf("GetExtendedTcpTable failed: %d", ret)
	}
	
	table := (*MIB_TCPTABLE_OWNER_PID)(unsafe.Pointer(&buf[0]))
	numEntries := int(table.NumEntries)
	
	if numEntries == 0 {
		return nil
	}
	
	entrySize := unsafe.Sizeof(MIB_TCPROW_OWNER_PID{})
	tableSize := unsafe.Sizeof(uint32(0))
	firstEntryPtr := uintptr(unsafe.Pointer(&buf[0])) + tableSize
	
	for i := 0; i < numEntries; i++ {
		entryPtr := (*MIB_TCPROW_OWNER_PID)(unsafe.Pointer(firstEntryPtr + uintptr(i)*entrySize))
		pid := entryPtr.OwningPid
		if pid > 0 {
			// Convert port from network byte order (big-endian) to little-endian
			localPort := uint32(entryPtr.LocalPort>>8) | uint32(entryPtr.LocalPort<<8)
			remotePort := uint32(entryPtr.RemotePort>>8) | uint32(entryPtr.RemotePort<<8)
			
			connInfo := TCPConnectionInfo{
				LocalAddr:  ipv4ToString(entryPtr.LocalAddr),
				LocalPort:  localPort,
				RemoteAddr: ipv4ToString(entryPtr.RemoteAddr),
				RemotePort: remotePort,
				State:      entryPtr.State,
			}
			result[pid] = append(result[pid], connInfo)
		}
	}
	
	return nil
}

func getTCPConnectionsIPv6Detailed(result map[uint32][]TCPConnectionInfo) error {
	var size uint32
	
	ret, _, _ := ProcGetExtendedTcpTable.Call(
		0,
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET6,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 122 {
		return fmt.Errorf("unexpected return value: %d", ret)
	}
	
	buf := make([]byte, size)
	ret, _, _ = ProcGetExtendedTcpTable.Call(
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET6,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 0 {
		return fmt.Errorf("GetExtendedTcpTable IPv6 failed: %d", ret)
	}
	
	table := (*MIB_TCP6TABLE_OWNER_PID)(unsafe.Pointer(&buf[0]))
	numEntries := int(table.NumEntries)
	
	if numEntries == 0 {
		return nil
	}
	
	entrySize := unsafe.Sizeof(MIB_TCP6ROW_OWNER_PID{})
	tableSize := unsafe.Sizeof(uint32(0))
	firstEntryPtr := uintptr(unsafe.Pointer(&buf[0])) + tableSize
	
	for i := 0; i < numEntries; i++ {
		entryPtr := (*MIB_TCP6ROW_OWNER_PID)(unsafe.Pointer(firstEntryPtr + uintptr(i)*entrySize))
		pid := entryPtr.OwningPid
		if pid > 0 {
			// Convert IPv6 address
			ipv6 := entryPtr.LocalAddr
			localAddr := fmt.Sprintf("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]",
				ipv6[0], ipv6[1], ipv6[2], ipv6[3], ipv6[4], ipv6[5], ipv6[6], ipv6[7],
				ipv6[8], ipv6[9], ipv6[10], ipv6[11], ipv6[12], ipv6[13], ipv6[14], ipv6[15])
			
			ipv6Remote := entryPtr.RemoteAddr
			remoteAddr := fmt.Sprintf("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]",
				ipv6Remote[0], ipv6Remote[1], ipv6Remote[2], ipv6Remote[3], ipv6Remote[4], ipv6Remote[5], ipv6Remote[6], ipv6Remote[7],
				ipv6Remote[8], ipv6Remote[9], ipv6Remote[10], ipv6Remote[11], ipv6Remote[12], ipv6Remote[13], ipv6Remote[14], ipv6Remote[15])
			
			// Convert port from network byte order (big-endian) to little-endian
			localPort := uint32(entryPtr.LocalPort>>8) | uint32(entryPtr.LocalPort<<8)
			remotePort := uint32(entryPtr.RemotePort>>8) | uint32(entryPtr.RemotePort<<8)
			
			connInfo := TCPConnectionInfo{
				LocalAddr:  localAddr,
				LocalPort:  localPort,
				RemoteAddr: remoteAddr,
				RemotePort: remotePort,
				State:      entryPtr.State,
			}
			result[pid] = append(result[pid], connInfo)
		}
	}
	
	return nil
}

func getTCPConnectionsIPv4(result map[uint32]bool) error {
	var size uint32
	
	// First call to get required buffer size
	ret, _, _ := ProcGetExtendedTcpTable.Call(
		0,
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 122 { // ERROR_INSUFFICIENT_BUFFER
		return fmt.Errorf("unexpected return value: %d", ret)
	}
	
	buf := make([]byte, size)
	ret, _, _ = ProcGetExtendedTcpTable.Call(
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 0 {
		return fmt.Errorf("GetExtendedTcpTable failed: %d", ret)
	}
	
	table := (*MIB_TCPTABLE_OWNER_PID)(unsafe.Pointer(&buf[0]))
	numEntries := int(table.NumEntries)
	
	if numEntries == 0 {
		return nil
	}
	
	// Calculate the offset to the first entry (after NumEntries)
	entrySize := unsafe.Sizeof(MIB_TCPROW_OWNER_PID{})
	tableSize := unsafe.Sizeof(uint32(0)) // Size of NumEntries
	firstEntryPtr := uintptr(unsafe.Pointer(&buf[0])) + tableSize
	
	for i := 0; i < numEntries; i++ {
		entryPtr := (*MIB_TCPROW_OWNER_PID)(unsafe.Pointer(firstEntryPtr + uintptr(i)*entrySize))
		pid := entryPtr.OwningPid
		if pid > 0 {
			result[pid] = true
		}
	}
	
	return nil
}

func getTCPConnectionsIPv6(result map[uint32]bool) error {
	var size uint32
	
	// First call to get required buffer size
	ret, _, _ := ProcGetExtendedTcpTable.Call(
		0,
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET6,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 122 { // ERROR_INSUFFICIENT_BUFFER
		return fmt.Errorf("unexpected return value: %d", ret)
	}
	
	buf := make([]byte, size)
	ret, _, _ = ProcGetExtendedTcpTable.Call(
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(unsafe.Pointer(&size)),
		0,
		AF_INET6,
		TCP_TABLE_OWNER_PID_ALL,
		0,
	)
	
	if ret != 0 {
		return fmt.Errorf("GetExtendedTcpTable IPv6 failed: %d", ret)
	}
	
	table := (*MIB_TCP6TABLE_OWNER_PID)(unsafe.Pointer(&buf[0]))
	numEntries := int(table.NumEntries)
	
	if numEntries == 0 {
		return nil
	}
	
	// Calculate the offset to the first entry (after NumEntries)
	entrySize := unsafe.Sizeof(MIB_TCP6ROW_OWNER_PID{})
	tableSize := unsafe.Sizeof(uint32(0)) // Size of NumEntries
	firstEntryPtr := uintptr(unsafe.Pointer(&buf[0])) + tableSize
	
	for i := 0; i < numEntries; i++ {
		entryPtr := (*MIB_TCP6ROW_OWNER_PID)(unsafe.Pointer(firstEntryPtr + uintptr(i)*entrySize))
		pid := entryPtr.OwningPid
		if pid > 0 {
			result[pid] = true
		}
	}
	
	return nil
}

