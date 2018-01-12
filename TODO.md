
# TODOs

- *Bug:* Potentially invoke rundown protection callback synchronously. 
    <br>1. Maybe by calling `_rundown.try_acquire()`. 
    <br>2. Maybe by synchronously `_rundown.release()`
    <br>HOW TO FIX IT: 
    <br>1. impl: `try_acquire()` may return false, but still requires a `release()`.
    <br>2. Trigger a notification to call `_rundown.release()`
- *Improvement:* Can we prevent unnecessary notification eventfd write?
- *Improvement:* Can we prevent unnecessary connection_async_send notification?
- *Improvement:* Use pooling for receive buffer
- *Test:* Test using UDS
- *Test:* Test closing a connection while sending
- *TODO:* `socket_environment.dispose()` how to implement?

# Fix by current commit

# Fixed
