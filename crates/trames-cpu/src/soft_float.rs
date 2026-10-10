// SPDX-License-Identifier: GPL-3.0-or-later
//! Integer-only IEEE binary32/binary64 arithmetic. No host floating environment.
use std::cmp::Ordering;

pub const INVALID: u32 = 1;
pub const DIVZERO: u32 = 2;
pub const OVERFLOW: u32 = 4;
pub const UNDERFLOW: u32 = 8;
pub const INEXACT: u32 = 16;
pub const DENORMAL: u32 = 128;

#[derive(Clone, Copy)]
pub struct Format {
    pub frac: u32,
    pub ebits: u32,
    pub bias: i32,
}
pub const SINGLE: Format = Format {
    frac: 23,
    ebits: 8,
    bias: 127,
};
pub const DOUBLE: Format = Format {
    frac: 52,
    ebits: 11,
    bias: 1023,
};
#[derive(Clone, Copy)]
pub struct Number {
    pub sign: bool,
    pub sig: u128,
    pub exp: i32,
    pub raw: u64,
    pub nan: bool,
    pub snan: bool,
    pub inf: bool,
}

fn jam(v: u128, shift: u32) -> u128 {
    if shift == 0 {
        v
    } else if shift >= 128 {
        u128::from(v != 0)
    } else {
        (v >> shift) | u128::from(v & ((1u128 << shift) - 1) != 0)
    }
}
fn scaled(v: u128, shift: i32) -> u128 {
    if shift >= 0 {
        v << shift
    } else {
        jam(v, (-shift) as u32)
    }
}
fn integer_sqrt(v: u128) -> u128 {
    let mut n = v;
    let mut result = 0;
    let mut bit = 1u128 << 126;
    while bit > n {
        bit >>= 2;
    }
    while bit != 0 {
        if n >= result + bit {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    result
}

impl Format {
    pub fn sign_bit(self) -> u64 {
        1u64 << (self.frac + self.ebits)
    }
    fn exp_mask(self) -> u64 {
        ((1u64 << self.ebits) - 1) << self.frac
    }
    pub fn quiet(self) -> u64 {
        1u64 << (self.frac - 1)
    }
    pub fn canonical(self) -> u64 {
        self.exp_mask() | self.quiet()
    }
    pub fn infinity(self, sign: bool) -> u64 {
        self.exp_mask() | if sign { self.sign_bit() } else { 0 }
    }
    pub fn unpack(self, raw: u64, fpcr: u32, flags: &mut u32) -> Number {
        let raw = raw & (self.sign_bit() | (self.sign_bit() - 1));
        let sign = raw & self.sign_bit() != 0;
        let field = ((raw & self.exp_mask()) >> self.frac) as i32;
        let frac = raw & ((1u64 << self.frac) - 1);
        let special = field == (1 << self.ebits) - 1;
        let nan = special && frac != 0;
        let mut sig = frac as u128;
        if field != 0 {
            sig |= 1u128 << self.frac;
        }
        if field == 0 && frac != 0 && fpcr & (1 << 24) != 0 {
            sig = 0;
            *flags |= DENORMAL;
        }
        Number {
            sign,
            sig,
            exp: field.max(1) - self.bias - self.frac as i32,
            raw,
            nan,
            snan: nan && frac & self.quiet() == 0,
            inf: special && frac == 0,
        }
    }
    pub fn nan_result(self, values: &[Number], fpcr: u32, flags: &mut u32) -> Option<u64> {
        let signaling = values.iter().find(|v| v.snan);
        if signaling.is_some() {
            *flags |= INVALID;
        }
        signaling
            .or_else(|| values.iter().find(|v| v.nan))
            .map(|n| {
                if fpcr & (1 << 25) != 0 {
                    self.canonical()
                } else {
                    n.raw | self.quiet()
                }
            })
    }
    /// Round `mag * 2^exp` once, preserving all guard/round/sticky information.
    pub fn pack(
        self,
        sign: bool,
        mag: u128,
        exp: i32,
        mode: u32,
        fpcr: u32,
        flags: &mut u32,
    ) -> u64 {
        let signbits = if sign { self.sign_bit() } else { 0 };
        if mag == 0 {
            return signbits;
        }
        let top = 127 - mag.leading_zeros() as i32;
        let emin = 1 - self.bias;
        let emax = self.bias;
        let tiny = top + exp < emin;
        if tiny && fpcr & (1 << 24) != 0 {
            *flags |= UNDERFLOW;
            return signbits;
        }
        let shift = (top - self.frac as i32).max(emin - self.frac as i32 - exp);
        let (mut sig, lost, half) = if shift <= 0 {
            (mag << -shift, false, Ordering::Less)
        } else if shift > 128 {
            (0, true, Ordering::Less)
        } else if shift == 128 {
            (0, true, mag.cmp(&(1u128 << 127)))
        } else {
            let remainder = mag & ((1u128 << shift) - 1);
            (
                mag >> shift,
                remainder != 0,
                remainder.cmp(&(1u128 << (shift - 1))),
            )
        };
        let increment = lost
            && match mode {
                0 => half == Ordering::Greater || (half == Ordering::Equal && sig & 1 != 0),
                1 => !sign,
                2 => sign,
                3 => false,
                4 => half != Ordering::Less,
                _ => unreachable!(),
            };
        sig += u128::from(increment);
        let mut result_exp = exp + shift + self.frac as i32;
        if sig >= 1u128 << (self.frac + 1) {
            sig >>= 1;
            result_exp += 1;
        }
        if result_exp > emax {
            *flags |= OVERFLOW | INEXACT;
            return if mode == 0 || mode == 4 || (mode == 1 && !sign) || (mode == 2 && sign) {
                self.infinity(sign)
            } else {
                signbits | (self.exp_mask() - 1)
            };
        }
        if lost {
            *flags |= INEXACT;
            if tiny {
                *flags |= UNDERFLOW;
            }
        }
        let field = if sig < 1u128 << self.frac {
            0
        } else {
            (result_exp + self.bias) as u64
        };
        signbits | (field << self.frac) | (sig as u64 & ((1u64 << self.frac) - 1))
    }
    pub fn add(self, a: Number, b: Number, mode: u32, fpcr: u32, flags: &mut u32) -> u64 {
        if let Some(n) = self.nan_result(&[a, b], fpcr, flags) {
            return n;
        }
        if a.inf || b.inf {
            if a.inf && b.inf && a.sign != b.sign {
                *flags |= INVALID;
                return self.canonical();
            }
            return self.infinity(if a.inf { a.sign } else { b.sign });
        }
        self.sum(
            a.sign, a.sig, a.exp, b.sign, b.sig, b.exp, mode, fpcr, flags,
        )
    }
    #[allow(clippy::too_many_arguments)]
    fn sum(
        self,
        sa: bool,
        a: u128,
        ea: i32,
        sb: bool,
        b: u128,
        eb: i32,
        mode: u32,
        fpcr: u32,
        flags: &mut u32,
    ) -> u64 {
        if a == 0 && b == 0 {
            return if (sa && sb) || (sa != sb && mode == 2) {
                self.sign_bit()
            } else {
                0
            };
        }
        let ta = if a == 0 {
            -10000
        } else {
            127 - a.leading_zeros() as i32 + ea
        };
        let tb = if b == 0 {
            -10000
        } else {
            127 - b.leading_zeros() as i32 + eb
        };
        let common = ta.max(tb) - 120;
        let va = if a == 0 { 0 } else { scaled(a, ea - common) };
        let vb = if b == 0 { 0 } else { scaled(b, eb - common) };
        let (sign, mag) = if sa == sb {
            (sa, va + vb)
        } else if va > vb {
            (sa, va - vb)
        } else if vb > va {
            (sb, vb - va)
        } else {
            (mode == 2, 0)
        };
        self.pack(sign, mag, common, mode, fpcr, flags)
    }
    pub fn mul(self, a: Number, b: Number, mode: u32, fpcr: u32, flags: &mut u32) -> u64 {
        if let Some(n) = self.nan_result(&[a, b], fpcr, flags) {
            return n;
        }
        if (a.inf && !b.inf && b.sig == 0) || (b.inf && !a.inf && a.sig == 0) {
            *flags |= INVALID;
            return self.canonical();
        }
        if a.inf || b.inf {
            return self.infinity(a.sign ^ b.sign);
        }
        self.pack(
            a.sign ^ b.sign,
            a.sig * b.sig,
            a.exp + b.exp,
            mode,
            fpcr,
            flags,
        )
    }
    pub fn div(self, a: Number, b: Number, mode: u32, fpcr: u32, flags: &mut u32) -> u64 {
        if let Some(n) = self.nan_result(&[a, b], fpcr, flags) {
            return n;
        }
        if (a.inf && b.inf) || (!a.inf && !b.inf && a.sig == 0 && b.sig == 0) {
            *flags |= INVALID;
            return self.canonical();
        }
        let sign = a.sign ^ b.sign;
        if a.inf {
            return self.infinity(sign);
        }
        if b.inf {
            return if sign { self.sign_bit() } else { 0 };
        }
        if b.sig == 0 {
            *flags |= DIVZERO;
            return self.infinity(sign);
        }
        if a.sig == 0 {
            return if sign { self.sign_bit() } else { 0 };
        }
        let anorm = self.frac as i32 - (127 - a.sig.leading_zeros() as i32);
        let bnorm = self.frac as i32 - (127 - b.sig.leading_zeros() as i32);
        let numerator = (a.sig << anorm) << 70;
        let denominator = b.sig << bnorm;
        let result = (numerator / denominator) | u128::from(!numerator.is_multiple_of(denominator));
        self.pack(
            sign,
            result,
            a.exp - anorm - b.exp + bnorm - 70,
            mode,
            fpcr,
            flags,
        )
    }
    pub fn sqrt(self, a: Number, mode: u32, fpcr: u32, flags: &mut u32) -> u64 {
        if let Some(n) = self.nan_result(&[a], fpcr, flags) {
            return n;
        }
        if a.sign && (a.inf || a.sig != 0) {
            *flags |= INVALID;
            return self.canonical();
        }
        if a.inf {
            return a.raw;
        }
        if a.sig == 0 {
            return if a.sign { self.sign_bit() } else { 0 };
        }
        let mut sig = a.sig;
        let mut e = a.exp;
        if e & 1 != 0 {
            sig <<= 1;
            e -= 1;
        }
        let shift = ((126 - (127 - sig.leading_zeros())) / 2) * 2;
        let scaled = sig << shift;
        let root = integer_sqrt(scaled);
        self.pack(
            false,
            root | u128::from(root * root != scaled),
            (e - shift as i32) / 2,
            mode,
            fpcr,
            flags,
        )
    }
    #[allow(clippy::too_many_arguments)]
    pub fn fma(
        self,
        mut a: Number,
        b: Number,
        mut c: Number,
        neg_product: bool,
        neg_c: bool,
        mode: u32,
        fpcr: u32,
        flags: &mut u32,
    ) -> u64 {
        // Negated forms negate their operands before ARM's NaN processing.
        if neg_product {
            a.sign = !a.sign;
            a.raw ^= self.sign_bit();
        }
        if neg_c {
            c.sign = !c.sign;
            c.raw ^= self.sign_bit();
        }
        // ARM processes NaNs in addend, first multiplicand, second multiplicand order.
        let bad_product =
            (a.inf && !b.inf && !b.nan && b.sig == 0) || (b.inf && !a.inf && !a.nan && a.sig == 0);
        if a.snan || b.snan || c.snan {
            return self
                .nan_result(&[c, a, b], fpcr, flags)
                .expect("signaling NaN");
        }
        if bad_product {
            *flags |= INVALID;
            return self.canonical();
        }
        if let Some(n) = self.nan_result(&[c, a, b], fpcr, flags) {
            return n;
        }
        let ps = a.sign ^ b.sign;
        let cs = c.sign;
        if a.inf || b.inf {
            if c.inf && ps != cs {
                *flags |= INVALID;
                return self.canonical();
            }
            return self.infinity(ps);
        }
        if c.inf {
            return self.infinity(cs);
        }
        self.sum(
            ps,
            a.sig * b.sig,
            a.exp + b.exp,
            cs,
            c.sig,
            c.exp,
            mode,
            fpcr,
            flags,
        )
    }
    pub fn compare(self, a: Number, b: Number, signaling: bool, flags: &mut u32) -> u32 {
        if a.nan || b.nan {
            if signaling || a.snan || b.snan {
                *flags |= INVALID;
            }
            return 3;
        }
        match self.order(a, b) {
            Ordering::Less => 8,
            Ordering::Equal => 6,
            Ordering::Greater => 2,
        }
    }
    pub fn order(self, a: Number, b: Number) -> Ordering {
        let av = if !a.inf && a.sig == 0 {
            0
        } else {
            a.raw & !self.sign_bit()
        };
        let bv = if !b.inf && b.sig == 0 {
            0
        } else {
            b.raw & !self.sign_bit()
        };
        if av == 0 && bv == 0 {
            Ordering::Equal
        } else if a.sign != b.sign {
            if a.sign {
                Ordering::Less
            } else {
                Ordering::Greater
            }
        } else if a.sign {
            bv.cmp(&av)
        } else {
            av.cmp(&bv)
        }
    }
    pub fn minmax(
        self,
        a: Number,
        b: Number,
        min: bool,
        numeric: bool,
        fpcr: u32,
        flags: &mut u32,
    ) -> u64 {
        let chosen = if numeric && !a.snan && !b.snan && a.nan != b.nan {
            Some(if a.nan { b } else { a })
        } else {
            None
        };
        if chosen.is_none() {
            if let Some(n) = self.nan_result(&[a, b], fpcr, flags) {
                return n;
            }
        }
        let n = chosen.unwrap_or_else(|| {
            if self.order(a, b) == Ordering::Equal && !a.inf && a.sig == 0 {
                if min == a.sign {
                    a
                } else {
                    b
                }
            } else if (self.order(a, b) == Ordering::Less) == min {
                a
            } else {
                b
            }
        });
        if n.inf {
            n.raw
        } else {
            self.pack(n.sign, n.sig, n.exp, 0, fpcr, flags)
        }
    }
    pub fn convert(self, source: Format, a: Number, mode: u32, fpcr: u32, flags: &mut u32) -> u64 {
        if a.nan {
            if a.snan {
                *flags |= INVALID;
            }
            if fpcr & (1 << 25) != 0 {
                return self.canonical();
            }
            let payload = a.raw & ((1u64 << source.frac) - 1);
            let mapped = if self.frac > source.frac {
                payload << (self.frac - source.frac)
            } else {
                payload >> (source.frac - self.frac)
            };
            return self.infinity(a.sign) | mapped | self.quiet();
        }
        if a.inf {
            return self.infinity(a.sign);
        }
        self.pack(a.sign, a.sig, a.exp, mode, fpcr, flags)
    }
    pub fn rounded_integer(self, a: Number, mode: u32) -> (u128, bool) {
        if a.exp >= 0 {
            return (a.sig.checked_shl(a.exp as u32).unwrap_or(u128::MAX), false);
        }
        let shift = (-a.exp) as u32;
        let (mut value, lost, half) = if shift >= 128 {
            (0, a.sig != 0, Ordering::Less)
        } else {
            let rem = a.sig & ((1u128 << shift) - 1);
            (a.sig >> shift, rem != 0, rem.cmp(&(1u128 << (shift - 1))))
        };
        if lost
            && match mode {
                0 => half == Ordering::Greater || (half == Ordering::Equal && value & 1 != 0),
                1 => !a.sign,
                2 => a.sign,
                3 => false,
                4 => half != Ordering::Less,
                _ => unreachable!(),
            }
        {
            value += 1;
        }
        (value, lost)
    }
    pub fn to_integer(
        self,
        mut a: Number,
        width: u32,
        unsigned: bool,
        scale: u32,
        mode: u32,
        flags: &mut u32,
    ) -> u64 {
        a.exp += scale as i32;
        let (mag, lost) = self.rounded_integer(a, mode);
        let max = if unsigned {
            (1u128 << width) - 1
        } else if a.sign {
            1u128 << (width - 1)
        } else {
            (1u128 << (width - 1)) - 1
        };
        let exceeds = a.exp >= 0 && (127 - a.sig.leading_zeros() as i32 + a.exp >= 128);
        if a.nan || a.inf || exceeds || mag > max || (unsigned && a.sign && mag != 0) {
            *flags |= INVALID;
            return if a.nan || (unsigned && a.sign) {
                0
            } else if a.sign {
                (0u64.wrapping_sub(max as u64))
                    & if width == 64 {
                        u64::MAX
                    } else {
                        u32::MAX as u64
                    }
            } else {
                max as u64
            };
        }
        if lost {
            *flags |= INEXACT;
        }
        if a.sign {
            0u64.wrapping_sub(mag as u64)
        } else {
            mag as u64
        }
    }
}
