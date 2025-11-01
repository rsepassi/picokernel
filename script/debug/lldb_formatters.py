#!/usr/bin/env python3
"""
LLDB Pretty-Printers for VMOS Kernel Structures

Usage in LLDB:
    command script import script/debug/lldb_formatters.py
"""

def kwork_summary(valobj, internal_dict):
    """Format kwork_t structure with human-readable state and operation"""

    # State names
    states = {
        0: 'DEAD',
        1: 'SUBMIT_REQUESTED',
        2: 'LIVE',
        3: 'READY',
        4: 'CANCEL_REQUESTED',
    }

    # Operation types
    ops = {
        1: 'TIMER',
        2: 'RNG_READ',
        3: 'BLOCK_READ',
        4: 'BLOCK_WRITE',
        5: 'BLOCK_FLUSH',
        6: 'NET_RECV',
        7: 'NET_SEND',
    }

    try:
        state_val = valobj.GetChildMemberWithName('state').GetValueAsUnsigned()
        op_val = valobj.GetChildMemberWithName('op').GetValueAsUnsigned()
        result_val = valobj.GetChildMemberWithName('result').GetValueAsUnsigned()
        flags_val = valobj.GetChildMemberWithName('flags').GetValueAsUnsigned()

        state_str = states.get(state_val, f'UNKNOWN({state_val})')
        op_str = ops.get(op_val, f'UNKNOWN({op_val})')

        # Build flag string
        flag_parts = []
        if flags_val & 0x01:
            flag_parts.append('STANDING')
        flags_str = '|'.join(flag_parts) if flag_parts else 'NONE'

        # Error code names
        err_names = {
            0: 'OK',
            1: 'BUSY',
            2: 'INVALID',
            3: 'CANCELLED',
            4: 'TIMEOUT',
            5: 'NO_DEVICE',
            6: 'IO_ERROR',
            7: 'NO_SPACE',
        }
        result_str = err_names.get(result_val, f'{result_val}')

        return f'kwork_t(op={op_str}, state={state_str}, result={result_str}, flags={flags_str})'
    except:
        return 'kwork_t(<error reading fields>)'


def ktimer_summary(valobj, internal_dict):
    """Format ktimer_req_t structure with deadline"""
    try:
        # Get embedded work structure
        work = valobj.GetChildMemberWithName('work')
        work_summary = kwork_summary(work, internal_dict)

        deadline = valobj.GetChildMemberWithName('deadline_ms').GetValueAsUnsigned()

        return f'ktimer_req_t(deadline={deadline}ms, {work_summary})'
    except:
        return 'ktimer_req_t(<error reading fields>)'


def krng_req_summary(valobj, internal_dict):
    """Format krng_req_t structure"""
    try:
        work = valobj.GetChildMemberWithName('work')
        work_summary = kwork_summary(work, internal_dict)

        length = valobj.GetChildMemberWithName('length').GetValueAsUnsigned()
        completed = valobj.GetChildMemberWithName('completed').GetValueAsUnsigned()

        return f'krng_req_t(len={length}, completed={completed}, {work_summary})'
    except:
        return 'krng_req_t(<error reading fields>)'


def kblk_req_summary(valobj, internal_dict):
    """Format kblk_req_t structure"""
    try:
        work = valobj.GetChildMemberWithName('work')
        work_summary = kwork_summary(work, internal_dict)

        num_segments = valobj.GetChildMemberWithName('num_segments').GetValueAsUnsigned()

        return f'kblk_req_t(segments={num_segments}, {work_summary})'
    except:
        return 'kblk_req_t(<error reading fields>)'


def knet_recv_req_summary(valobj, internal_dict):
    """Format knet_recv_req_t structure"""
    try:
        work = valobj.GetChildMemberWithName('work')
        work_summary = kwork_summary(work, internal_dict)

        num_buffers = valobj.GetChildMemberWithName('num_buffers').GetValueAsUnsigned()
        buffer_index = valobj.GetChildMemberWithName('buffer_index').GetValueAsUnsigned()

        return f'knet_recv_req_t(bufs={num_buffers}, cur={buffer_index}, {work_summary})'
    except:
        return 'knet_recv_req_t(<error reading fields>)'


def knet_send_req_summary(valobj, internal_dict):
    """Format knet_send_req_t structure"""
    try:
        work = valobj.GetChildMemberWithName('work')
        work_summary = kwork_summary(work, internal_dict)

        num_packets = valobj.GetChildMemberWithName('num_packets').GetValueAsUnsigned()
        packets_sent = valobj.GetChildMemberWithName('packets_sent').GetValueAsUnsigned()

        return f'knet_send_req_t(pkts={num_packets}, sent={packets_sent}, {work_summary})'
    except:
        return 'knet_send_req_t(<error reading fields>)'


def kernel_summary(valobj, internal_dict):
    """Format kernel_t structure with queue depths and timer count"""
    try:
        # Count items in queues by walking linked lists
        def count_list(head_name, has_prev=False):
            count = 0
            head = valobj.GetChildMemberWithName(head_name).GetValueAsUnsigned()
            if head == 0:
                return 0

            # Safety limit to prevent infinite loops
            visited = set()
            current_addr = head

            while current_addr != 0 and current_addr not in visited and count < 1000:
                visited.add(current_addr)
                count += 1

                # Read 'next' pointer from kwork_t structure
                # Assuming next is at offset 24 for 64-bit (after op, callback, ctx, result, state, flags)
                error = lldb.SBError()
                process = valobj.GetProcess()

                # Try to read the structure's next pointer
                # This is fragile - better to use actual type information
                try:
                    next_ptr = process.ReadPointerFromMemory(current_addr + 24, error)
                    if error.Success():
                        current_addr = next_ptr
                    else:
                        break
                except:
                    break

            return count

        submit_depth = count_list('submit_queue_head')
        ready_depth = count_list('ready_queue_head')
        cancel_depth = count_list('cancel_queue_head')

        timer_count = valobj.GetChildMemberWithName('timer_heap_size').GetValueAsUnsigned()
        current_time = valobj.GetChildMemberWithName('current_time_ms').GetValueAsUnsigned()

        return (f'kernel_t(time={current_time}ms, '
                f'submit={submit_depth}, ready={ready_depth}, cancel={cancel_depth}, '
                f'timers={timer_count})')
    except Exception as e:
        return f'kernel_t(<error: {e}>)'


def platform_summary(valobj, internal_dict):
    """Format platform_t structure with device presence"""
    try:
        has_rng = valobj.GetChildMemberWithName('virtio_rng_ptr').GetValueAsUnsigned() != 0
        has_blk = valobj.GetChildMemberWithName('virtio_blk_ptr').GetValueAsUnsigned() != 0
        has_net = valobj.GetChildMemberWithName('virtio_net_ptr').GetValueAsUnsigned() != 0

        devices = []
        if has_rng:
            devices.append('RNG')
        if has_blk:
            blk_capacity = valobj.GetChildMemberWithName('block_capacity').GetValueAsUnsigned()
            blk_sector = valobj.GetChildMemberWithName('block_sector_size').GetValueAsUnsigned()
            devices.append(f'BLK({blk_capacity}x{blk_sector}B)')
        if has_net:
            # Read MAC address
            mac_field = valobj.GetChildMemberWithName('net_mac_address')
            if mac_field.IsValid():
                mac_bytes = []
                for i in range(6):
                    byte_val = mac_field.GetChildAtIndex(i).GetValueAsUnsigned()
                    mac_bytes.append(f'{byte_val:02x}')
                mac_str = ':'.join(mac_bytes)
                devices.append(f'NET({mac_str})')
            else:
                devices.append('NET')

        device_str = ', '.join(devices) if devices else 'no devices'

        return f'platform_t({device_str})'
    except Exception as e:
        return f'platform_t(<error: {e}>)'


def __lldb_init_module(debugger, internal_dict):
    """Register all formatters with LLDB"""
    print("Loading VMOS kernel formatters...")

    # Register type summaries
    debugger.HandleCommand('type summary add -F lldb_formatters.kwork_summary kwork_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.ktimer_summary ktimer_req_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.krng_req_summary krng_req_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.kblk_req_summary kblk_req_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.knet_recv_req_summary knet_recv_req_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.knet_send_req_summary knet_send_req_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.kernel_summary kernel_t')
    debugger.HandleCommand('type summary add -F lldb_formatters.platform_summary platform_t')

    print("VMOS formatters loaded successfully!")
    print("Try: p kernel, p work, p *timer_req")
