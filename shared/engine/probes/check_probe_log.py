#!/usr/bin/env python3
"""Compare fetched-neighbour hashes against separately rendered current-input hashes.

A passing comparison establishes only consistency in this capture. Numbered test footage and
host-case records are still required: equal/repeated source images cannot prove delivered time.
"""
import argparse
import json
import sys


def key(record, neighbour=False):
    path = record['host_path']
    bounds = ()
    if path == 'ofx-cpu':
        time = record['requested_neighbour_time'] if neighbour else record['time']
        scale = (record['scale_x'], record['scale_y'])
        quality = (record['draft'], record['interactive'])
        bounds = (record['neighbour_bounds_x'], record['neighbour_bounds_y']) if neighbour else (record['bounds_x'], record['bounds_y'])
    elif path == 'premiere-cpu':
        time = record['requested_neighbour_ticks'] if neighbour else record['time_ticks']
        scale = (record['downsample_x_num'], record['downsample_x_den'], record['downsample_y_num'], record['downsample_y_den'])
        quality = (record['quality'], record['time_scale'])
    elif path == 'premiere-metal':
        time = record['requested_neighbour_sequence_ticks'] if neighbour else record['sequence_time_ticks']
        scale = (record['downsample_x'], record['downsample_y'])
        quality = (record['quality'], record['ticks_per_frame'], record['field_type'])
    else:
        raise ValueError(f'unknown host_path: {path}')
    width = record['neighbour_width'] if neighbour else record['width']
    height = record['neighbour_height'] if neighbour else record['height']
    return path, time, scale, quality, record.get('format', record.get('layout')), width, height, bounds


def check(records):
    current = {}
    compared = mismatches = missing = conflicts = unavailable = geometry = 0
    for r in records:
        if r.get('schema') != 'vv-temporal-probe-v1' or r.get('action') != 'render':
            continue
        k = key(r)
        if k in current and current[k] != r['current_hash']:
            conflicts += 1
        current[k] = r['current_hash']
    for r in records:
        if r.get('schema') != 'vv-temporal-probe-v1' or r.get('action') != 'render':
            continue
        if not r['neighbour_available']:
            unavailable += 1
            continue
        invalid_geometry = (r.get('same_bounds') is False or
                            r['neighbour_width'] != r['width'] or r['neighbour_height'] != r['height'])
        if r['host_path'] == 'ofx-cpu':
            invalid_geometry |= ((r['bounds_x'], r['bounds_y']) !=
                                 (r['neighbour_bounds_x'], r['neighbour_bounds_y']))
        if invalid_geometry:
            geometry += 1
            continue
        expected = current.get(key(r, True))
        if expected is None:
            missing += 1
        else:
            compared += 1
            mismatches += expected != r['neighbour_hash']
    return dict(compared=compared, mismatches=mismatches, missing_reference=missing,
                inconsistent_current=conflicts, unavailable_neighbours=unavailable, geometry_mismatches=geometry)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', help='one capture/case JSONL, not unrelated host sessions mixed together')
    args = p.parse_args()
    try:
        with open(args.log, encoding='utf-8') as f:
            records = [json.loads(line) for line in f if line.strip()]
        result = check(records)
    except (OSError, ValueError, KeyError, TypeError) as e:
        p.error(str(e))
    print(json.dumps(result, sort_keys=True))
    print('Consistency only; numbered footage and independent host-case review are still required.')
    # Missing references/frames are incomplete evidence, not a silent pass.
    return 0 if result['compared'] and not any(result[k] for k in result if k != 'compared') else 1


if __name__ == '__main__':
    sys.exit(main())
