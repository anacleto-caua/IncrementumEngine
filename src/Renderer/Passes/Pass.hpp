#pragma once

class Pass {
public:
    virtual ~Pass() = default;

    virtual IncResult Init() = 0;
    virtual void Destroy() = 0;
    virtual void Render() = 0;

    // Most passes don't stream data every frame - default no-op instead of forcing every
    // override to provide an empty body.
    virtual void FrameSensibleTransfers() {}
};
