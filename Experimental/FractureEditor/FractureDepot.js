//============================================================================================================================================
//                                                              FRACTUREDEPOT.JS
//============================================================================================================================================
// 📦 Transactional browser storage for closed fragment geometry.

// One browser-local fragment receipt per scene ID. Native asset serialization is deferred.
async function Connect() {
  return new Promise((Resolve, Reject) => {
    const Request = indexedDB.open("Frontier.Fracture.v2", 1);
    Request.onupgradeneeded = () =>
      Request.result.createObjectStore("Fragments");
    Request.onerror = () => Reject(Request.error);
    Request.onsuccess = () => Resolve(Request.result);
  });
}
export async function ReadFragments(Id) {
  const Connection = await Connect();
  try {
    return await new Promise((Resolve, Reject) => {
      const Request = Connection.transaction("Fragments")
        .objectStore("Fragments")
        .get(Id);
      Request.onsuccess = () => Resolve(Request.result || null);
      Request.onerror = () => Reject(Request.error);
    });
  } finally {
    Connection.close();
  }
}
export async function WriteFragments(Id, Record) {
  const Connection = await Connect();
  try {
    await new Promise((Resolve, Reject) => {
      const Transaction = Connection.transaction("Fragments", "readwrite");
      const Collection = Transaction.objectStore("Fragments");
      if (Record) Collection.put(Record, Id);
      else Collection.delete(Id);
      Transaction.oncomplete = Resolve;
      Transaction.onerror = () => Reject(Transaction.error);
      Transaction.onabort = () =>
        Reject(Transaction.error || new Error("Fragment storage aborted"));
    });
  } finally {
    Connection.close();
  }
}
