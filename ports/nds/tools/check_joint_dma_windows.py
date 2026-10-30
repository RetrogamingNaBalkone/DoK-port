"""Check captured C7+ map DMA timing independently of product audit counters."""
import argparse
import hashlib
import json
from pathlib import Path


def check(path, gpu=False):
    raw = path.read_bytes()
    report = json.loads(raw)
    rows = report['profile_rows'] if 'profile_rows' in report else report.get('rows', [])
    assert rows, ('No complete stage profiles', path)
    history = report.get('history_records')
    history_source = None
    if gpu and history is None:
        history_path = path.parents[1] / 'emulator/result.json'
        history_raw = history_path.read_bytes()
        history = json.loads(history_raw)['history_records']
        history_source = dict(path=str(history_path), sha256=hashlib.sha256(history_raw).hexdigest())
    presented = {}
    for row in history or []:
        if not row['flags'] & 8:
            continue
        assert row['frame_id'] not in presented, ('Duplicate PRESENTED history', path, row['frame_id'])
        presented[row['frame_id']] = row
    if gpu:
        ids = [row['frame_id'] for row in rows]
        assert len(ids) == len(set(ids)), ('Duplicate CPU profiles', path)
        assert set(ids) == set(presented), ('Incomplete upload/profile coverage', path,
            sorted(set(ids)-set(presented)), sorted(set(presented)-set(ids)))
    transfers = []
    gpu_transfers = []
    inherited_cold = 0
    for row in rows:
        if gpu:
            upload = presented[row['frame_id']]
            assert (row['reserved63'] != 0xffffffff) == bool(upload['upload_bytes']), (
                'GPU upload marker disagrees with PRESENTED bytes', path, row['frame_id'],
                row['reserved63'], upload['upload_bytes'])
        if gpu and row['reserved63'] != 0xffffffff:
            packed = row['reserved63']
            gpu_start, gpu_end = packed & 65535, packed >> 16
            if upload['flags'] & 1:
                # Pre-render cold preload can contain several guarded chunks.
                # 3D is off and the post-swap deadline does not apply. Packed
                # lines describe its final chunk; stream_commit measures the
                # later normal-call site and cannot time the whole preload.
                inherited_cold += 1
            else:
                assert gpu_start <= 47 or 192 <= gpu_start <= 262, ('GPU upload start', path, row['frame_id'], gpu_start)
                assert gpu_end <= 47 or 192 <= gpu_end <= 262, ('GPU upload end', path, row['frame_id'], gpu_end)
                # The accepted window is a single interval crossing frame end:
                # 192..262, then0..47. Passing through48..191 is not permitted.
                start_unwrapped = gpu_start if gpu_start >= 192 else gpu_start+263
                end_unwrapped = gpu_end if gpu_end >= 192 else gpu_end+263
                assert start_unwrapped <= end_unwrapped, ('GPU upload crossed active interval', path, row['frame_id'])
                assert row['stream_commit'] * 60 < row['timer_hz'], ('GPU upload spans a video frame', path, row['frame_id'])
                gpu_transfers.append(dict(frame_id=row['frame_id'], start=gpu_start, end=gpu_end,
                                          duration_ms=row['stream_commit']*1000/row['timer_hz']))
        start, end = row['reserved60'], row['reserved61']
        if not row['map_dma']:
            assert start == end == 0xffffffff, ('Idle DMA markers', path, row['frame_id'], start, end)
            continue
        assert 192 <= start <= end <= 262, ('Map DMA outside VBlank', path, row['frame_id'], start, end)
        # With elapsed CPU ticks below one video frame, identical line numbers
        # cannot conceal one or more complete video periods.
        assert row['map_dma'] * 60 < row['timer_hz'], ('DMA spans a video frame', path, row['frame_id'])
        transfers.append(dict(frame_id=row['frame_id'], start=start, end=end,
                              duration_ms=row['map_dma']*1000/row['timer_hz']))
    assert transfers, ('No actual map DMA observed', path)
    return dict(profile=str(path), profile_sha256=hashlib.sha256(raw).hexdigest(),
                profile_frames=len(rows), transfers=len(transfers),
                minimum_start_line=min(row['start'] for row in transfers),
                maximum_end_line=max(row['end'] for row in transfers),
                maximum_duration_ms=max(row['duration_ms'] for row in transfers),
                idle_frames=len(rows)-len(transfers),
                ordered_gpu_upload_frames=len(gpu_transfers) if gpu else None,
                inherited_cold_final_chunk_samples=inherited_cold if gpu else None,
                presented_upload_coverage_checked=gpu,
                upload_history_source=history_source,
                maximum_ordered_gpu_commit_ms=max((row['duration_ms'] for row in gpu_transfers), default=None),
                ordered_gpu_uploads=gpu_transfers if gpu else None, passed=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profiles', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--gpu', action='store_true', help='C8+ packed normal GPU commit window; inherited pre-render cold loops retain their product guards')
    args = parser.parse_args()
    report = dict(task='N21P', passed=True,
                  scope='Every actual map DMA in supplied complete stage captures; includes cold/loading and normal frames',
                  criterion='192 <= start <= end <= 262; duration < one video frame; no-draw markers both UINT32_MAX',
                  gpu_scope='Changed-order normal stream commits only; packed profile63. Product also checks absolute upload epoch. Inherited cold pre-render upload loops use the unchanged guards.' if args.gpu else None,
                  observer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  profiles=[check(path, args.gpu) for path in args.profiles])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', 'utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
