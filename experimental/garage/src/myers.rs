// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Myers `O(ND)` shortest-edit-script sequence aligner.
//!
//! Drop-in replacement for the `similar::{Algorithm, DiffOp,
//! capture_diff_slices}` slice aligner used by [`crate::diff`].

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Algorithm {
    Myers,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiffOp {
    Equal {
        old_index: usize,
        new_index: usize,
        len: usize,
    },
    Delete {
        old_index: usize,
        old_len: usize,
        new_index: usize,
    },
    Insert {
        old_index: usize,
        new_index: usize,
        new_len: usize,
    },
    Replace {
        old_index: usize,
        old_len: usize,
        new_index: usize,
        new_len: usize,
    },
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum RawStep {
    Equal,
    Delete,
    Insert,
}

/// Computes the Myers diff between `old` and `new` and coalesces adjacent
/// `Delete` + `Insert` runs into `DiffOp::Replace` (matching `similar`).
pub fn capture_diff_slices<T: PartialEq>(_alg: Algorithm, old: &[T], new: &[T]) -> Vec<DiffOp> {
    if old.is_empty() && new.is_empty() {
        return Vec::new();
    }
    if old.is_empty() {
        return vec![DiffOp::Insert {
            old_index: 0,
            new_index: 0,
            new_len: new.len(),
        }];
    }
    if new.is_empty() {
        return vec![DiffOp::Delete {
            old_index: 0,
            old_len: old.len(),
            new_index: 0,
        }];
    }

    // Trim shared prefix and suffix before running Myers so identical
    // regions cost `O(N)` instead of `O(ND)`.
    let mut prefix = 0;
    while prefix < old.len() && prefix < new.len() && old[prefix] == new[prefix] {
        prefix += 1;
    }
    let mut old_end = old.len();
    let mut new_end = new.len();
    while old_end > prefix && new_end > prefix && old[old_end - 1] == new[new_end - 1] {
        old_end -= 1;
        new_end -= 1;
    }

    let steps = myers_steps(&old[prefix..old_end], &new[prefix..new_end]);
    let mut ops = Vec::new();
    let mut oi = 0usize;
    let mut ni = 0usize;

    if prefix > 0 {
        ops.push(DiffOp::Equal {
            old_index: 0,
            new_index: 0,
            len: prefix,
        });
        oi = prefix;
        ni = prefix;
    }

    let mut idx = 0;
    while idx < steps.len() {
        let step = steps[idx];
        let mut count = 1;
        while idx + count < steps.len() && steps[idx + count] == step {
            count += 1;
        }
        idx += count;

        match step {
            RawStep::Equal => {
                push_op(
                    &mut ops,
                    DiffOp::Equal {
                        old_index: oi,
                        new_index: ni,
                        len: count,
                    },
                );
                oi += count;
                ni += count;
            }
            RawStep::Delete => {
                push_op(
                    &mut ops,
                    DiffOp::Delete {
                        old_index: oi,
                        old_len: count,
                        new_index: ni,
                    },
                );
                oi += count;
            }
            RawStep::Insert => {
                push_op(
                    &mut ops,
                    DiffOp::Insert {
                        old_index: oi,
                        new_index: ni,
                        new_len: count,
                    },
                );
                ni += count;
            }
        }
    }

    let suffix = old.len() - old_end;
    if suffix > 0 {
        push_op(
            &mut ops,
            DiffOp::Equal {
                old_index: oi,
                new_index: ni,
                len: suffix,
            },
        );
    }

    ops
}

fn push_op(ops: &mut Vec<DiffOp>, next: DiffOp) {
    match (ops.last().copied(), next) {
        (
            Some(DiffOp::Equal {
                old_index,
                new_index,
                len,
            }),
            DiffOp::Equal {
                old_index: o2,
                new_index: n2,
                len: l2,
            },
        ) if old_index + len == o2 && new_index + len == n2 => {
            *ops.last_mut().unwrap() = DiffOp::Equal {
                old_index,
                new_index,
                len: len + l2,
            };
        }
        (
            Some(DiffOp::Delete {
                old_index, old_len, ..
            }),
            DiffOp::Insert {
                new_index, new_len, ..
            },
        )
        | (
            Some(DiffOp::Insert {
                new_index, new_len, ..
            }),
            DiffOp::Delete {
                old_index, old_len, ..
            },
        ) => {
            *ops.last_mut().unwrap() = DiffOp::Replace {
                old_index,
                old_len,
                new_index,
                new_len,
            };
        }
        (
            Some(DiffOp::Replace {
                old_index,
                old_len,
                new_index,
                new_len,
            }),
            DiffOp::Delete {
                old_len: more_old, ..
            },
        ) => {
            *ops.last_mut().unwrap() = DiffOp::Replace {
                old_index,
                old_len: old_len + more_old,
                new_index,
                new_len,
            };
        }
        (
            Some(DiffOp::Replace {
                old_index,
                old_len,
                new_index,
                new_len,
            }),
            DiffOp::Insert {
                new_len: more_new, ..
            },
        ) => {
            *ops.last_mut().unwrap() = DiffOp::Replace {
                old_index,
                old_len,
                new_index,
                new_len: new_len + more_new,
            };
        }
        _ => ops.push(next),
    }
}

/// Linear-space Hirschberg + Myers divide-and-conquer so even huge phases
/// with thousands of unmatched rows never blow memory (`O(N + M)` space).
fn myers_steps<T: PartialEq>(old: &[T], new: &[T]) -> Vec<RawStep> {
    let mut out = Vec::with_capacity(old.len() + new.len());
    hirschberg(old, new, &mut out);
    out
}

fn hirschberg<T: PartialEq>(old: &[T], new: &[T], out: &mut Vec<RawStep>) {
    let mut start = 0;
    while start < old.len() && start < new.len() && old[start] == new[start] {
        out.push(RawStep::Equal);
        start += 1;
    }
    let old = &old[start..];
    let new = &new[start..];

    let mut suffix = 0;
    while suffix < old.len()
        && suffix < new.len()
        && old[old.len() - 1 - suffix] == new[new.len() - 1 - suffix]
    {
        suffix += 1;
    }
    let core_old = &old[..old.len() - suffix];
    let core_new = &new[..new.len() - suffix];

    if core_old.is_empty() {
        out.extend(std::iter::repeat_n(RawStep::Insert, core_new.len()));
    } else if core_new.is_empty() {
        out.extend(std::iter::repeat_n(RawStep::Delete, core_old.len()));
    } else if let Some(steps) = bounded_myers_trace(core_old, core_new, 512) {
        out.extend(steps);
    } else if let Some((x, y, u, v)) = find_middle_snake(core_old, core_new) {
        hirschberg(&core_old[..x], &core_new[..y], out);
        out.extend(std::iter::repeat_n(RawStep::Equal, u - x));
        hirschberg(&core_old[u..], &core_new[v..], out);
    } else {
        out.extend(std::iter::repeat_n(RawStep::Delete, core_old.len()));
        out.extend(std::iter::repeat_n(RawStep::Insert, core_new.len()));
    }

    out.extend(std::iter::repeat_n(RawStep::Equal, suffix));
}

/// Classic forward Myers when `D <= limit` (fast path for typical phase diffs).
fn bounded_myers_trace<T: PartialEq>(old: &[T], new: &[T], limit: usize) -> Option<Vec<RawStep>> {
    let n = old.len() as isize;
    let m = new.len() as isize;
    let max_d = ((n + m) as usize).min(limit);
    let offset = max_d as isize + 1;
    let size = (2 * offset + 2) as usize;
    let mut v = vec![0isize; size];
    let mut trace: Vec<Vec<isize>> = Vec::new();

    for d in 0..=max_d as isize {
        trace.push(v.clone());
        let mut k = -d;
        while k <= d {
            let idx = (k + offset) as usize;
            let mut x = if k == -d
                || (k != d && v[(k - 1 + offset) as usize] < v[(k + 1 + offset) as usize])
            {
                v[(k + 1 + offset) as usize]
            } else {
                v[(k - 1 + offset) as usize] + 1
            };
            let mut y = x - k;
            while x < n && y < m && old[x as usize] == new[y as usize] {
                x += 1;
                y += 1;
            }
            v[idx] = x;
            if x >= n && y >= m {
                return Some(backtrack(&trace, n, m, offset));
            }
            k += 2;
        }
    }
    None
}

fn backtrack(trace: &[Vec<isize>], mut x: isize, mut y: isize, offset: isize) -> Vec<RawStep> {
    let mut rev = Vec::with_capacity((x + y) as usize);
    for d in (0..trace.len() as isize).rev() {
        let v = &trace[d as usize];
        let k = x - y;
        let prev_k =
            if k == -d || (k != d && v[(k - 1 + offset) as usize] < v[(k + 1 + offset) as usize]) {
                k + 1
            } else {
                k - 1
            };
        let prev_x = v[(prev_k + offset) as usize];
        let prev_y = prev_x - prev_k;

        while x > prev_x && y > prev_y {
            rev.push(RawStep::Equal);
            x -= 1;
            y -= 1;
        }
        if d > 0 {
            if x == prev_x {
                rev.push(RawStep::Insert);
                y -= 1;
            } else {
                rev.push(RawStep::Delete);
                x -= 1;
            }
        }
    }
    rev.reverse();
    rev
}

/// Myers 1986 §4b middle-snake finder in `O(N + M)` space for large `D`.
fn find_middle_snake<T: PartialEq>(old: &[T], new: &[T]) -> Option<(usize, usize, usize, usize)> {
    let n = old.len() as isize;
    let m = new.len() as isize;
    let max = n + m;
    if max == 0 {
        return None;
    }
    let delta = n - m;
    let odd = delta.rem_euclid(2) == 1;
    let half = (max + 1) / 2;
    let offset = half + 2;
    let size = (2 * offset + 2) as usize;
    let mut vf = vec![0isize; size];
    let mut vb = vec![0isize; size];

    for d in 0..=half {
        let mut k = -d;
        while k <= d {
            let idx = (k + offset) as usize;
            let mut x = if k == -d
                || (k != d && vf[(k - 1 + offset) as usize] < vf[(k + 1 + offset) as usize])
            {
                vf[(k + 1 + offset) as usize]
            } else {
                vf[(k - 1 + offset) as usize] + 1
            };
            let mut y = x - k;
            let x0 = x;
            let y0 = y;
            while x < n && y < m && old[x as usize] == new[y as usize] {
                x += 1;
                y += 1;
            }
            vf[idx] = x;
            if odd && (k - delta).abs() <= d - 1 {
                let b_idx = (-(k - delta) + offset) as usize;
                if x + vb[b_idx] >= n {
                    return Some((x0 as usize, y0 as usize, x as usize, y as usize));
                }
            }
            k += 2;
        }

        let mut k = -d;
        while k <= d {
            let idx = (k + offset) as usize;
            let mut x = if k == -d
                || (k != d && vb[(k - 1 + offset) as usize] < vb[(k + 1 + offset) as usize])
            {
                vb[(k + 1 + offset) as usize]
            } else {
                vb[(k - 1 + offset) as usize] + 1
            };
            let mut y = x - k;
            let mut u = n - x;
            let mut v = m - y;
            while x < n && y < m && old[(n - 1 - x) as usize] == new[(m - 1 - y) as usize] {
                x += 1;
                y += 1;
            }
            vb[idx] = x;
            if !odd && (k - (-delta)).abs() <= d {
                let f_idx = (-(k + delta) + offset) as usize;
                if vf[f_idx] + x >= n {
                    let x0 = (n - x) as usize;
                    let y0 = (m - y) as usize;
                    u = u.max(x0 as isize);
                    v = v.max(y0 as isize);
                    return Some((x0, y0, u as usize, v as usize));
                }
            }
            k += 2;
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reconstruct(old: &[i32], new: &[i32], ops: &[DiffOp]) -> (Vec<i32>, Vec<i32>) {
        let mut lo = Vec::new();
        let mut ro = Vec::new();
        for op in ops {
            match *op {
                DiffOp::Equal {
                    old_index,
                    new_index,
                    len,
                } => {
                    assert_eq!(
                        &old[old_index..old_index + len],
                        &new[new_index..new_index + len]
                    );
                    lo.extend_from_slice(&old[old_index..old_index + len]);
                    ro.extend_from_slice(&new[new_index..new_index + len]);
                }
                DiffOp::Delete {
                    old_index, old_len, ..
                } => {
                    lo.extend_from_slice(&old[old_index..old_index + old_len]);
                }
                DiffOp::Insert {
                    new_index, new_len, ..
                } => {
                    ro.extend_from_slice(&new[new_index..new_index + new_len]);
                }
                DiffOp::Replace {
                    old_index,
                    old_len,
                    new_index,
                    new_len,
                } => {
                    lo.extend_from_slice(&old[old_index..old_index + old_len]);
                    ro.extend_from_slice(&new[new_index..new_index + new_len]);
                }
            }
        }
        (lo, ro)
    }

    #[test]
    fn small_and_large_diffs_round_trip() {
        let a = [1, 2, 3, 4, 5];
        let b = [1, 9, 3, 5, 6];
        let ops = capture_diff_slices(Algorithm::Myers, &a, &b);
        assert_eq!(reconstruct(&a, &b, &ops), (a.to_vec(), b.to_vec()));

        // Exercise the Hirschberg fallback (`D > 512`).
        let big_a: Vec<i32> = (0..700).collect();
        let big_b: Vec<i32> = (350..1050).collect();
        let ops = capture_diff_slices(Algorithm::Myers, &big_a, &big_b);
        assert_eq!(reconstruct(&big_a, &big_b, &ops), (big_a, big_b));
    }
}
