#include "VideoFramePresenter.h"
#include <cstdlib>
#include <iostream>

static void check(bool ok, const char* name)
{
    if(!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}

int main()
{
    AVFrame dummy{};
    AVFramePtr frame(&dummy, [](AVFrame*){});
    VideoFramePresenter presenter;
    AVFramePtr taken;
    quint64 seq = 0;
    quint64 session = 0;

    const auto first = presenter.Publish(frame);
    check(first.notify && !first.overwritten, "first frame requests repaint");
    check(presenter.Publish(frame).overwritten, "unfetched frame is dropped");
    check(presenter.TakeFrameForPaint(taken,seq,session) && seq == 2,
          "GUI takes newest frame");
    check(!presenter.Publish(frame).overwritten, "in-flight frame is not dropped");
    check(presenter.Publish(frame).overwritten, "next untaken frame is dropped");

    presenter.Reset();
    check(!presenter.OnNotified(first.sessionId), "old notification is ignored");
    presenter.Publish(frame);
    check(!presenter.MarkPainted(seq,session), "old paint cannot advance new session");
    check(presenter.TakeFrameForPaint(taken,seq,session) && seq == 1,
          "new session first frame remains available");
    check(presenter.MarkPainted(seq,session), "current session paint succeeds");
    check(!presenter.TakeFrameForPaint(taken,seq,session), "painted frame is consumed");
    std::cout << "PASS: presenter session and overwrite behavior\n";
}
