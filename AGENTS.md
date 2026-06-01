# Agent Instructions

## Project Requirement

The project must address the football automatic broadcasting assignment requirement:

- Compare the advantages and disadvantages of existing football match automatic broadcasting solutions.
- Design an automatic broadcasting scheme that can automatically switch views and generate broadcast output.
- Design automatic editing workflows for both personal highlights and full-match highlights.
- Build an evaluation metric system for the automatic broadcast and highlight outputs.
- Select suitable supporting hardware for the proposed solution.
- Implement the matching software in C++.

## Scope And Constraints

- The implementation language is C++.
- The solution should focus on football match automatic broadcasting and highlight generation.
- The design should cover both system architecture and practical hardware/software integration.
- Documentation should make the comparison, scheme design, evaluation indicators, hardware choices, and implementation plan clear enough for assignment review.
- Team submission limit: at most 3 groups.
- Weight: 1.2.

## Innovation Perspective

When comparing against existing football match production modes, the project should emphasize the following innovative position:

- Traditional professional football broadcasting has high visual quality and rich manual camera language, but it depends on many camera operators, directors, replay staff, and expensive venue infrastructure. This project should target a lower-cost and repeatable automatic broadcast workflow for campus, amateur, and training matches.
- Existing AI sports cameras can automatically record, track the ball, stream matches, and generate simple clips, but many solutions mainly solve "follow the action" rather than "understand the match". This project should highlight match-aware directing: camera switching should consider ball position, attack direction, player density, penalty-area threat, set pieces, counterattacks, and tactical context.
- Existing football technologies such as VAR, goal-line technology, and semi-automated offside focus on referee decision support and accuracy, not on audience-facing broadcast storytelling. This project should reuse the idea of structured tracking data, but redirect it toward automatic viewing, replay selection, and highlight generation.
- The proposed system should combine live broadcasting and post-match editing in one event timeline. Real-time detection results should drive camera switching during the match, then be reused after the match to produce full-match highlights and player-specific personal highlights.
- The system should introduce an explainable highlight score instead of only cutting around goals or shots. The score may combine event type, field zone, attacking threat, player involvement, action continuity, score impact, and replay value.
- The project should support both broadcast quality and coaching value. A normal viewer needs smooth view switching and clear key events, while coaches and players need panoramic tactical context, personal clips, and measurable performance evidence.
- The evaluation should not stop at detection accuracy. It should also measure ball-in-frame rate, key-player-in-frame rate, missed key events, switching smoothness, tactical information retention, highlight relevance, clip redundancy, processing latency, and subjective viewer satisfaction.

Recommended core innovation statement: build a C++ football automatic broadcasting and highlight system that is not just an automatic camera tracker, but a match-aware, explainable, low-cost automatic director for live output, full-match highlights, and player-specific personal highlights.
