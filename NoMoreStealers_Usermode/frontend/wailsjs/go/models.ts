export namespace app {
	
	export class Event {
	    type: string;
	    processName: string;
	    pid: number;
	    path: string;
	    isSigned: boolean;
	    timestamp: string;
	
	    static createFrom(source: any = {}) {
	        return new Event(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.type = source["type"];
	        this.processName = source["processName"];
	        this.pid = source["pid"];
	        this.path = source["path"];
	        this.isSigned = source["isSigned"];
	        this.timestamp = source["timestamp"];
	    }
	}

}

