export namespace app {
	
	export class Event {
	    type: string;
	    processName: string;
	    pid: number;
	    executablePath: string;
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
	        this.executablePath = source["executablePath"];
	        this.path = source["path"];
	        this.isSigned = source["isSigned"];
	        this.timestamp = source["timestamp"];
	    }
	}
	export class StringSummary {
	    count: number;
	    samples: string[];
	    keywords?: Record<string, Array<string>>;
	
	    static createFrom(source: any = {}) {
	        return new StringSummary(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.count = source["count"];
	        this.samples = source["samples"];
	        this.keywords = source["keywords"];
	    }
	}
	export class PESectionInfo {
	    name: string;
	    virtualSize: string;
	    virtualAddress: string;
	    rawSize: string;
	    entropy: number;
	    entropyLabel: string;
	    permissions?: string[];
	    risk: string;
	
	    static createFrom(source: any = {}) {
	        return new PESectionInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.virtualSize = source["virtualSize"];
	        this.virtualAddress = source["virtualAddress"];
	        this.rawSize = source["rawSize"];
	        this.entropy = source["entropy"];
	        this.entropyLabel = source["entropyLabel"];
	        this.permissions = source["permissions"];
	        this.risk = source["risk"];
	    }
	}
	export class PEAnalysis {
	    architecture: string;
	    fileType: string;
	    entryPoint?: string;
	    imageBase?: string;
	    numberOfSections: number;
	    sizeOfImage?: string;
	    importCount?: number;
	    exportCount?: number;
	    sections?: PESectionInfo[];
	
	    static createFrom(source: any = {}) {
	        return new PEAnalysis(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.architecture = source["architecture"];
	        this.fileType = source["fileType"];
	        this.entryPoint = source["entryPoint"];
	        this.imageBase = source["imageBase"];
	        this.numberOfSections = source["numberOfSections"];
	        this.sizeOfImage = source["sizeOfImage"];
	        this.importCount = source["importCount"];
	        this.exportCount = source["exportCount"];
	        this.sections = this.convertValues(source["sections"], PESectionInfo);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class VirusTotalReport {
	    status: string;
	    hash: string;
	    link?: string;
	    lastAnalysisDate?: string;
	    malicious?: number;
	    suspicious?: number;
	    undetected?: number;
	    harmless?: number;
	    notes?: string[];
	
	    static createFrom(source: any = {}) {
	        return new VirusTotalReport(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.status = source["status"];
	        this.hash = source["hash"];
	        this.link = source["link"];
	        this.lastAnalysisDate = source["lastAnalysisDate"];
	        this.malicious = source["malicious"];
	        this.suspicious = source["suspicious"];
	        this.undetected = source["undetected"];
	        this.harmless = source["harmless"];
	        this.notes = source["notes"];
	    }
	}
	export class FileDetails {
	    path: string;
	    exists: boolean;
	    isDir: boolean;
	    size?: number;
	    modified?: string;
	    created?: string;
	    sha256?: string;
	    hashAvailable: boolean;
	    hashSkippedReason?: string;
	    isSigned: boolean;
	    notes?: string[];
	    firstSeen?: string;
	    virusTotal?: VirusTotalReport;
	    pe?: PEAnalysis;
	    strings?: StringSummary;
	
	    static createFrom(source: any = {}) {
	        return new FileDetails(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.exists = source["exists"];
	        this.isDir = source["isDir"];
	        this.size = source["size"];
	        this.modified = source["modified"];
	        this.created = source["created"];
	        this.sha256 = source["sha256"];
	        this.hashAvailable = source["hashAvailable"];
	        this.hashSkippedReason = source["hashSkippedReason"];
	        this.isSigned = source["isSigned"];
	        this.notes = source["notes"];
	        this.firstSeen = source["firstSeen"];
	        this.virusTotal = this.convertValues(source["virusTotal"], VirusTotalReport);
	        this.pe = this.convertValues(source["pe"], PEAnalysis);
	        this.strings = this.convertValues(source["strings"], StringSummary);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class EventDetails {
	    eventType: string;
	    source: string;
	    timestamp: string;
	    processName: string;
	    pid: number;
	    isProcessRunning: boolean;
	    executable?: FileDetails;
	    target?: FileDetails;
	    targetRaw?: string;
	    notes?: string[];
	
	    static createFrom(source: any = {}) {
	        return new EventDetails(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.eventType = source["eventType"];
	        this.source = source["source"];
	        this.timestamp = source["timestamp"];
	        this.processName = source["processName"];
	        this.pid = source["pid"];
	        this.isProcessRunning = source["isProcessRunning"];
	        this.executable = this.convertValues(source["executable"], FileDetails);
	        this.target = this.convertValues(source["target"], FileDetails);
	        this.targetRaw = source["targetRaw"];
	        this.notes = source["notes"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	
	
	
	export class Settings {
	    virusTotalApiKey: string;
	
	    static createFrom(source: any = {}) {
	        return new Settings(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.virusTotalApiKey = source["virusTotalApiKey"];
	    }
	}
	

}

