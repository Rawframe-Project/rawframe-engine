// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Springs, decay and the rubber band:
// a damped spring pulling a value to its target, and a velocity decaying
// to rest, both solved in closed form from where they start, so the same
// times give the same values at every frame rate; and iOS's rubber band
// for dragging past a limit. A spring's start is an offset from the
// target and a velocity, in the value's units (per second); a retarget
// keeps the velocity and changes the offset.

#ifndef MAUL_TWEEN_SPRING_H
#define MAUL_TWEEN_SPRING_H

#include "maul-tween/base.h"
#include "maul-tween/easing.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A spring: its natural frequency and damping ratio, with what the
    // samples need precomputed. Make one with a mtwnMakeSpring... function;
    // its fields are the library's, and a host reads none of them. Every
    // call taking one refuses a spring no maker produced: its precomputed
    // fields are derived again from the frequency and ratio and compared
    // bit for bit.
    typedef struct mtwnSpring
    {
        double parameters[5];
        uint32_t regime;
    } mtwnSpring;

    // How a spring moves a start over one time: the offset at the time is
    // offsetFromOffset times the start's offset plus offsetFromVelocity
    // times its velocity, and the velocity likewise.
    typedef struct mtwnSpringMotion
    {
        double offsetFromOffset;
        double offsetFromVelocity;
        double velocityFromOffset;
        double velocityFromVelocity;
    } mtwnSpringMotion;

    /// Makes a spring from its physical parameters: natural frequency
    /// sqrt(stiffness / mass), damping ratio damping / (2 sqrt(stiffness
    /// mass)).
    ///
    /// @param stiffness  Finite and above 0.
    /// @param damping    Finite and at least 0.
    /// @param mass       Finite and above 0.
    /// @param springOut  Receives the spring; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         parameter outside the above or giving a frequency outside the
    ///         finite range above 0.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeSpring(double stiffness, double damping, double mass,
                                                      mtwnSpring* springOut);

    /// Makes a spring from its natural frequency in hertz and its damping
    /// ratio: 1 is critical, below it overshoots, 0 never settles.
    ///
    /// @param hertz         Finite and above 0.
    /// @param dampingRatio  Finite and at least 0.
    /// @param springOut     Receives the spring; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         parameter outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeSpringFromFrequency(double hertz,
                                                                   double dampingRatio,
                                                                   mtwnSpring* springOut);

    /// Makes a spring from Apple's perceptual duration and bounce, as
    /// SwiftUI, Motion and anime.js map them: natural frequency 2 pi /
    /// duration, damping ratio 1 - bounce for a bounce from 0, 1 / (1 +
    /// bounce) below it.
    ///
    /// @param duration   Seconds, finite and above 0.
    /// @param bounce     Above -1 and at most 1: 0 is critical, 1 never
    ///                   settles, below 0 is overdamped.
    /// @param springOut  Receives the spring; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         parameter outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeSpringFromDuration(double duration, double bounce,
                                                                  mtwnSpring* springOut);

    /// Computes how a spring moves any start over a time, for applying to
    /// every channel of a value.
    ///
    /// @param spring     A spring made by this library.
    /// @param seconds    The time since the start, at least 0; infinity
    ///                   gives the rest, all four zero when the spring is
    ///                   damped.
    /// @param motionOut  Receives the motion; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a spring
    ///         no maker produced, or a time outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeSpringMotion(const mtwnSpring* spring,
                                                               double seconds,
                                                               mtwnSpringMotion* motionOut);

    /// Samples a spring from a start.
    ///
    /// @param spring       A spring made by this library.
    /// @param offset       The start's offset from the target, finite.
    /// @param velocity     The start's velocity, per second, finite.
    /// @param seconds      The time since the start, at least 0; infinity
    ///                     gives the rest.
    /// @param offsetOut    Receives the offset then; may be NULL; unchanged
    ///                     on failure.
    /// @param velocityOut  Receives the velocity then; may be NULL;
    ///                     unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null spring, a spring
    ///         no maker produced, or a start or time outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleSpring(const mtwnSpring* spring, double offset,
                                                        double velocity, double seconds,
                                                        double* offsetOut, double* velocityOut);

    /// Returns the time after which a spring from a start stays within
    /// both tolerances of its target and of rest, by bounds on its offset
    /// and velocity that never end it early; the host then snaps it to the
    /// target.
    ///
    /// @param spring             A spring made by this library.
    /// @param offset             The start's offset from the target, finite.
    /// @param velocity           The start's velocity, per second, finite.
    /// @param offsetTolerance    How far from the target is at rest, above
    ///                           0.
    /// @param velocityTolerance  How fast is at rest, per second, above 0.
    /// @param secondsOut         Receives the seconds from the start: 0
    ///                           when it starts within both; infinity for an
    ///                           undamped spring that does not. Unchanged on
    ///                           failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a spring
    ///         no maker produced, or a start or tolerance outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeSpringSettlingTime(const mtwnSpring* spring,
                                                                     double offset, double velocity,
                                                                     double offsetTolerance,
                                                                     double velocityTolerance,
                                                                     double* secondsOut);

    /// Makes an easing of a spring over a duration: the spring from an
    /// offset of -1 with a start velocity, sampled at progress times the
    /// duration and divided by its value at the duration, so it is exactly
    /// 0 at 0 and 1 at 1; a spring settled by then is changed by the
    /// distance it had left. For CSS linear() (mtwnFitLinearEasing) and for
    /// tracks timed by duration. An underdamped spring oscillating faster
    /// than once in 2^-10 of the duration is refused, as an elastic's period
    /// is: a fit of MTWN_MAX_FIT_POINTS points could not follow it. So is
    /// one whose eased progress could pass the doubles' range, as a spring
    /// almost back at its start at the duration has, divided by its value
    /// there. So is one whose natural frequency, in radians a second, times
    /// the duration reaches 2^512 (about 1.34e154): its square, the easing's
    /// squared frequency, would pass the doubles' range, as mtwnMakeSpring
    /// refuses a spring whose own would. Only an overdamped or critical
    /// spring reaches it, an oscillating one being refused far sooner.
    ///
    /// @param spring     A spring made by this library.
    /// @param duration   Seconds, finite and above 0.
    /// @param velocity   The start velocity in units of the whole change a
    ///                   second, finite: 0 from rest.
    /// @param mode       The mode.
    /// @param easingOut  Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         spring no maker produced, an unknown mode, a duration or
    ///         velocity outside the above, a spring back at its start at
    ///         the duration, or one of the three above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeSpringEasing(const mtwnSpring* spring,
                                                            double duration, double velocity,
                                                            mtwnEaseMode mode,
                                                            mtwnEasing* easingOut);

    // A decay: a velocity falling exponentially at a rate a second, as a
    // fling coasts to rest. Make one with mtwnMakeDecay,
    // mtwnMakeDecayFromDeceleration or mtwnMakeDecayToRest; every call
    // taking one refuses a rate no maker gives, not finite and above 0.
    typedef struct mtwnDecay
    {
        double rate;
    } mtwnDecay;

    /// Makes a decay whose velocity falls as e^(-rate t).
    ///
    /// @param rate      The rate a second, finite and above 0.
    /// @param decayOut  Receives the decay; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a rate
    ///         outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeDecay(double rate, mtwnDecay* decayOut);

    /// Makes a decay from iOS's deceleration rate, the share of velocity
    /// kept each millisecond (0.998 normal, 0.99 fast): the rate a second
    /// is -1000 ln of it.
    ///
    /// @param perMillisecond  Above 0 and below 1.
    /// @param decayOut        Receives the decay; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         share outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeDecayFromDeceleration(double perMillisecond,
                                                                     mtwnDecay* decayOut);

    /// Samples a decay from a start: the position is position + velocity
    /// (1 - e^(-k t)) / k, the velocity velocity e^(-k t).
    ///
    /// @param decay        A decay made by this library.
    /// @param position     The start's position, finite.
    /// @param velocity     The start's velocity, per second, finite.
    /// @param seconds      The time since the start, at least 0; infinity
    ///                     gives the rest.
    /// @param positionOut  Receives the position then; may be NULL;
    ///                     unchanged on failure.
    /// @param velocityOut  Receives the velocity then; may be NULL;
    ///                     unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null decay, a decay no
    ///         maker produced, or a start or time outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleDecay(const mtwnDecay* decay, double position,
                                                       double velocity, double seconds,
                                                       double* positionOut, double* velocityOut);

    /// Returns the time a decay's speed falls to a stop speed.
    ///
    /// @param decay       A decay made by this library.
    /// @param velocity    The start's velocity, per second, finite.
    /// @param stopSpeed   The speed counted as stopped, per second, above 0.
    /// @param secondsOut  Receives the seconds from the start: 0 when the
    ///                    start is no faster. Unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a decay
    ///         no maker produced, or a velocity or stop speed outside the
    ///         above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeDecayStopTime(const mtwnDecay* decay,
                                                                double velocity, double stopSpeed,
                                                                double* secondsOut);

    /// Returns the time a decay reaches a position, where a host hands it
    /// over to a spring back from a limit.
    ///
    /// @param decay       A decay made by this library.
    /// @param position    The start's position, finite.
    /// @param velocity    The start's velocity, per second, finite.
    /// @param target      The position to reach, finite.
    /// @param secondsOut  Receives the seconds from the start: 0 when it
    ///                    starts there; infinity when it never gets there,
    ///                    moving away or coming to rest short of it or
    ///                    exactly on it. Unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a decay
    ///         no maker produced, or a value outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeDecayCrossingTime(const mtwnDecay* decay,
                                                                    double position,
                                                                    double velocity, double target,
                                                                    double* secondsOut);

    /// Makes the decay that comes to rest at an end from a start, keeping
    /// the start's velocity: its rate is velocity / (end
    /// - position). With mtwnSampleDecay's rest (seconds infinite) as the
    /// natural end, a host snaps a fling: it chooses the end, a page or a
    /// grid point near the natural one, and lands there.
    ///
    /// @param position  The start's position.
    /// @param velocity  The start's velocity, per second.
    /// @param rest      The end to come to rest at.
    /// @param decayOut  Receives the decay; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a value
    ///         not finite, an end the velocity does not move toward (behind
    ///         the start, at it, or a velocity of 0), or a rate that
    ///         overflows.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeDecayToRest(double position, double velocity,
                                                           double rest, mtwnDecay* decayOut);

    // A decay held between bounds: it coasts while
    // within them; where it would come to rest past one, a spring takes
    // it from the bound, at the exact time it reaches the bound and with
    // the velocity it has there, back to the bound; a start past a bound
    // is the spring's from the start. As Flutter's
    // BouncingScrollSimulation and Motion's inertia with min and max, the
    // crossing found exactly. Make one with mtwnMakeBoundedDecay.
    typedef struct mtwnBoundedDecay
    {
        mtwnDecay decay;
        mtwnSpring spring;
        double lower;
        double upper;
    } mtwnBoundedDecay;

    /// Makes a bounded decay.
    ///
    /// @param decay    A decay made by this library.
    /// @param spring   A spring made by this library, the bounce.
    /// @param lower    The lower bound; may be -infinity, for none.
    /// @param upper    The upper bound, at least the lower; may be
    ///                 infinity, for none.
    /// @param boundedOut  Receives the bounded decay; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a decay
    ///         or spring no maker produced, a NaN bound, a lower bound above
    ///         the upper, a lower bound of infinity or an upper of
    ///         -infinity.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeBoundedDecay(const mtwnDecay* decay,
                                                            const mtwnSpring* spring, double lower,
                                                            double upper,
                                                            mtwnBoundedDecay* boundedOut);

    /// Samples a bounded decay from a start.
    ///
    /// @param bounded      A bounded decay made by this library.
    /// @param position     The start's position, finite.
    /// @param velocity     The start's velocity, per second, finite.
    /// @param seconds      The time since the start, at least 0; infinity
    ///                     gives the rest.
    /// @param positionOut  Receives the position then; may be NULL;
    ///                     unchanged on failure.
    /// @param velocityOut  Receives the velocity then; may be NULL;
    ///                     unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null bounded decay,
    ///         one no maker produced, or a start or time outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleBoundedDecay(const mtwnBoundedDecay* bounded,
                                                              double position, double velocity,
                                                              double seconds, double* positionOut,
                                                              double* velocityOut);

    /// Returns the time a bounded decay from a start comes to rest: a
    /// coast's stop at the stop speed, or the spring's settling within
    /// the offset tolerance of the bound and the stop speed after it takes
    /// over (mtwnComputeSpringSettlingTime).
    ///
    /// @param bounded          A bounded decay made by this library.
    /// @param position         The start's position, finite.
    /// @param velocity         The start's velocity, per second, finite.
    /// @param offsetTolerance  How far from the bound the spring is at
    ///                         rest, above 0.
    /// @param stopSpeed        The speed counted as stopped, per second,
    ///                         above 0.
    /// @param secondsOut       Receives the seconds from the start;
    ///                         infinity for an undamped spring. Unchanged
    ///                         on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         bounded decay no maker produced, or a start or tolerance
    ///         outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeBoundedDecayStopTime(
        const mtwnBoundedDecay* bounded, double position, double velocity, double offsetTolerance,
        double stopSpeed, double* secondsOut);

    /// iOS's rubber band: how far content moves when dragged a distance
    /// past a limit, in a dimension (the view's size along the drag):
    /// c x d / (c x + d), with slope c at the limit, never reaching d.
    /// Signed: a negative distance moves the other way.
    ///
    /// @param distance     The drag past the limit, not NaN; may be infinite.
    /// @param dimension    The view's size along it, above 0; may be infinite.
    /// @param coefficient  c, 0.55 on iOS; above 0, and may be infinite.
    /// @param offsetOut    Receives the distance moved; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null offsetOut or an
    ///         argument outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnApplyRubberBand(double distance, double dimension,
                                                           double coefficient, double* offsetOut);

    /// The inverse of mtwnApplyRubberBand: the drag that moves content an
    /// offset past a limit, d o / (c (d - o)).
    ///
    /// @param offset       The distance moved, signed, not NaN.
    /// @param dimension    The view's size along it, above 0; may be infinite.
    /// @param coefficient  c, 0.55 on iOS; above 0, and may be infinite.
    /// @param dragOut      Receives the drag: infinite, signed, for an offset
    ///                     of at least the dimension; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null dragOut or an
    ///         argument outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnInvertRubberBand(double offset, double dimension,
                                                            double coefficient, double* dragOut);

#ifdef __cplusplus
}
#endif

#endif
