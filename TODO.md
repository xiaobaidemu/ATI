
# TODOs

- *Improvement:* Use pooling for receive buffer
- *Test:* Test closing a connection while sending
- *TODO:* `socket_environment.dispose()` - how to implement?
- *TODO:* Where to `delete` created socket_listeners as well as created/accepted socket_connections?
- *Enhancement*: Allow `OnReceive` returns a value indicating how many bytes are NOT used (and will be prepended to next OnReceive)
- *Test*: Test `async_send_many`

# Fix by current commit
- *Enhancement*: Allow sending multiple fragments in one `async_send` call (with their order preserved)

# Fixed

- *Test:* Test using UDS
- *Bug:* Potentially invoke rundown protection callback synchronously. 
    <br>1. Maybe by calling `_rundown.try_acquire()`. 
    <br>2. Maybe by synchronously `_rundown.release()`
    <br>HOW TO FIX IT: 
    <br>1. impl: `try_acquire()` may return false, but still requires a `release()`.
    <br>2. Trigger a notification to call `_rundown.release()`
- *Improvement:* Can we prevent unnecessary connection_async_send notification?
- *Improvement:* Can we prevent unnecessary notification eventfd write?
