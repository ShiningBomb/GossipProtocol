/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 *              Definition of MP1Node class functions.
 **********************************/

#include "MP1Node.h"

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log, Address *address)
{
    for (int i = 0; i < 6; i++)
    {
        NULLADDR[i] = 0;
    }
    this->memberNode = member;
    this->emulNet = emul;
    this->log = log;
    this->par = params;
    this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node() {}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 *              This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop()
{
    if (memberNode->bFailed)
    {
        return false;
    }
    else
    {
        return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size)
{
    Queue q;
    return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 *              All initializations routines for a member.
 *              Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport)
{
    Address joinaddr;
    joinaddr = getJoinAddress();

    // Self booting routines
    if (initThisNode(&joinaddr) == -1)
    {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if (!introduceSelfToGroup(&joinaddr))
    {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr)
{
    /*
     * This function is partially implemented and may require changes
     */
    // int id = *(int*)(&memberNode->addr.addr);
    // int port = *(short*)(&memberNode->addr.addr[4]);

    memberNode->bFailed = false;
    memberNode->inited = true;
    memberNode->inGroup = false;
    // node is up!
    memberNode->nnb = 0;
    memberNode->heartbeat = 0;
    memberNode->pingCounter = TFAIL;
    memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode);

    return 0;
}

/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr)
{
    MessageHdr *msg;
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if (0 == memcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr), sizeof(memberNode->addr.addr)))
    {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        addSelfToGroup();
    }
    else
    {
        size_t msgsize = sizeof(MessageHdr) + sizeof(joinaddr->addr) + sizeof(long) + 1;
        msg = (MessageHdr *)malloc(msgsize * sizeof(char));

        // create JOINREQ message: format of data is {struct Address myaddr}
        msg->msgType = JOINREQ;
        memcpy((char *)(msg + 1), &memberNode->addr.addr, sizeof(memberNode->addr.addr));
        memcpy((char *)(msg + 1) + 1 + sizeof(memberNode->addr.addr), &memberNode->heartbeat, sizeof(long));
#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREQ message to introducer member
        emulNet->ENsend(&memberNode->addr, joinaddr, (char *)msg, msgsize);
        free(msg);
    }

    return 1;
}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode()
{
    /*
    * Your code goes here
    */
    return 0;
}

/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 *              Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop()
{
    if (memberNode->bFailed)
    {
        return;
    }

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if (!memberNode->inGroup)
    {
        return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages()
{
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while (!memberNode->mp1q.empty())
    {
        ptr = memberNode->mp1q.front().elt;
        size = memberNode->mp1q.front().size;
        memberNode->mp1q.pop();
        recvCallBack((void *)memberNode, (char *)ptr, size);
    }
    return;
}

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(void *env, char *data, int size)
{
    MessageHdr *msg = (MessageHdr *)data;

    // JOINREQ
    if (msg->msgType == JOINREQ)
    {
        Address *joiningAddr = (Address *)malloc(sizeof(Address));
        memcpy(&joiningAddr->addr, ((char *)msg) + sizeof(MessageHdr), sizeof(joiningAddr->addr));

        // send JOINREP
        GossipMsgHdr *repmsg;
        int msgsize = buildGossip(JOINREP, &repmsg);
        emulNet->ENsend(&memberNode->addr, joiningAddr, (char *)repmsg, msgsize);
        free(repmsg);
    }
    // JOINREP
    else if (msg->msgType == JOINREP)
    {

        GossipMsgHdr *joinmsg = (GossipMsgHdr *)data;
        vector<MemberListEntry> newMemberList;
        unpackGossip(joinmsg, &newMemberList);
        addSelfToGroup();
        appendMembership(&newMemberList);
    }
    // GOSSIP
    else if (msg->msgType == GOSSIP)
    {
        GossipMsgHdr *gossipmsg = (GossipMsgHdr *)data;
        vector<MemberListEntry> newMemberList;
        unpackGossip(gossipmsg, &newMemberList);
        appendMembership(&newMemberList);
    }

    return true;
}

void MP1Node::appendMembership(vector<MemberListEntry> *newMemberList)
{
    for (int i = 0; i < newMemberList->size(); i++)
    {
        bool exist = false;
        for (int s = 0; s < memberNode->memberList.size(); s++)
        {
            if ((*newMemberList)[i].getport() == memberNode->memberList[s].getport() && (*newMemberList)[i].getid() == memberNode->memberList[s].getid())
            {
                exist = true;
                long newHeartbeat = (*newMemberList)[i].getheartbeat();
                if (newHeartbeat > memberNode->memberList[s].getheartbeat())
                {
                    memberNode->memberList[s].settimestamp(par->getcurrtime());
                    memberNode->memberList[s].setheartbeat(newHeartbeat);
                }
                break;
            }
        }

        if (!exist)
        {
#ifdef DEBUGLOG
            string address = to_string((*newMemberList)[i].getid()) + ":" + to_string((*newMemberList)[i].getport());
            Address toAddr = Address(address);
            log->logNodeAdd(&memberNode->addr, &toAddr);
#endif
            memberNode->nnb++;
            memberNode->memberList.push_back(MemberListEntry((*newMemberList)[i].getid(), (*newMemberList)[i].getport(), (*newMemberList)[i].getheartbeat(), par->getcurrtime()));
        }
    }
}

/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 *              the nodes
 *              Propagate your membership list
 */
void MP1Node::nodeLoopOps()
{
    int k = 2;

    if (memberNode->nnb == 0)
        return;

    // check for failed nodes
    for (int i = memberNode->nnb; i >= 1; i--)
    {
        // no heartbeat for TFAIL + TREMOVE seconds - we remove the node here
        if (par->globaltime - memberNode->memberList[i].timestamp > (TFAIL + TREMOVE))
        {
            Address toAddr = createAddress(memberNode->memberList[i]);
            memberNode->memberList.erase(memberNode->memberList.begin() + i);
            memberNode->nnb--;

#ifdef DEBUGLOG
            log->logNodeRemove(&memberNode->addr, &toAddr);
#endif
        }
    }

    // increase heartbeat of self
    memberNode->heartbeat++;
    memberNode->memberList[0].setheartbeat(memberNode->heartbeat);

    // send membership list
    GossipMsgHdr *gossip_msg;

    int msgsize = buildGossip(GOSSIP, &gossip_msg);

    // gossip to k random nodes
    for (int i = 0; i < k; i++)
    {
        int n = (rand() % memberNode->nnb) + 1;
        MemberListEntry randomNode = memberNode->memberList[n];
        Address toAddr = createAddress(randomNode);
        emulNet->ENsend(&memberNode->addr, &toAddr, (char *)gossip_msg, msgsize);
    }

    free(gossip_msg);

    return;
}

/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr)
{
    return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress()
{
    Address joinaddr;

    memset(&joinaddr, 0, sizeof(Address));
    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode)
{
    memberNode->memberList.clear();
}

/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n", addr->addr[0], addr->addr[1], addr->addr[2],
           addr->addr[3], *(short *)&addr->addr[4]);
}

void MP1Node::addSelfToGroup()
{
    int id;
    short port;

    // node is now part of the group
    memberNode->inGroup = true;

    // create memberlist entry for self
    memcpy(&id, &memberNode->addr.addr[0], sizeof(int));
    memcpy(&port, &memberNode->addr.addr[4], sizeof(short));
    memberNode->memberList.push_back(MemberListEntry(id, port, memberNode->heartbeat, par->getcurrtime()));
    memberNode->myPos = memberNode->memberList.begin();
}

int MP1Node::buildGossip(enum MsgTypes msgType, GossipMsgHdr **msg)
{
    int memberCount = 0;

    size_t msgheadersize = sizeof(GossipMsgHdr);
    size_t msgsize = msgheadersize + (sizeof(MemberListEntry) * memberNode->memberList.size()) + 1;
    *msg = (GossipMsgHdr *)malloc(msgsize * sizeof(char));

    (*msg)->msgType = msgType;

    for (int i = 0; i < memberNode->memberList.size(); i++)
    {
        // only gossip about non-faulty members
        if (i == 0 || !hasFailed(memberNode->memberList[i]))
        {
            memcpy((char *)*msg + msgheadersize + (memberCount * sizeof(MemberListEntry)), &(memberNode->memberList[i]), sizeof(MemberListEntry));
            memberCount++;
        }
    }

    (*msg)->numEntries = memberCount;

    return msgsize;
}

void MP1Node::unpackGossip(GossipMsgHdr *gossipMessage, vector<MemberListEntry> *newMemberList)
{
    int numEntries = gossipMessage->numEntries;
    int id;
    short port;
    long heartbeat;
    char *temp_address;

    for (int i = 0; i < numEntries; i++)
    {
        temp_address = (char *)gossipMessage + sizeof(GossipMsgHdr) + (i * sizeof(MemberListEntry));
        memcpy(&id, temp_address, sizeof(int));
        memcpy(&port, temp_address + sizeof(int), sizeof(short));
        memcpy(&heartbeat, temp_address + sizeof(int) + sizeof(int), sizeof(long));
        (*newMemberList).push_back(MemberListEntry(id, port, heartbeat, 0));
    }
}

bool MP1Node::hasFailed(const MemberListEntry &m)
{
    if (par->globaltime - m.timestamp > TFAIL)
    {
        return true;
    }

    return false;
}

Address MP1Node::createAddress(MemberListEntry &m)
{
    string address = to_string(m.getid()) + ":" + to_string(m.getport());
    Address toAddr = Address(address);
    return toAddr;
}
