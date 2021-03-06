/*************************************************************************
 * Copyright 2014 Eucalyptus Systems, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * Please contact Eucalyptus Systems, Inc., 6755 Hollister Ave., Goleta
 * CA 93117, USA or visit http://www.eucalyptus.com/licenses/ if you
 * need additional information or have any questions.
 *
 * This file may incorporate work covered under the following copyright
 * and permission notice:

 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * 
 *  http://aws.amazon.com/apache2.0
 * 
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
package com.eucalyptus.simpleworkflow.common.model;

import java.io.Serializable;
import static com.eucalyptus.simpleworkflow.common.model.SimpleWorkflowMessage.FieldRegex;
import static com.eucalyptus.simpleworkflow.common.model.SimpleWorkflowMessage.FieldRegexValue;

/**
 * <p>
 * Provides details for the <code>StartLambdaFunctionFailed</code> event.
 * </p>
 */
public class StartLambdaFunctionFailedEventAttributes implements Serializable {

    /**
     * The ID of the <code>LambdaFunctionScheduled</code> event that was
     * recorded when this AWS Lambda function was scheduled. This information
     * can be useful for diagnosing problems by tracing back the chain of
     * events leading up to this event.
     */
    private Long scheduledEventId;

    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     */
    private String cause;

    /**
     * The error message (if any).
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Length: </b>0 - 1728<br/>
     */
    @FieldRegex( FieldRegexValue.OPT_STRING_1728 )
    private String message;

    /**
     * The ID of the <code>LambdaFunctionScheduled</code> event that was
     * recorded when this AWS Lambda function was scheduled. This information
     * can be useful for diagnosing problems by tracing back the chain of
     * events leading up to this event.
     *
     * @return The ID of the <code>LambdaFunctionScheduled</code> event that was
     *         recorded when this AWS Lambda function was scheduled. This information
     *         can be useful for diagnosing problems by tracing back the chain of
     *         events leading up to this event.
     */
    public Long getScheduledEventId() {
        return scheduledEventId;
    }
    
    /**
     * The ID of the <code>LambdaFunctionScheduled</code> event that was
     * recorded when this AWS Lambda function was scheduled. This information
     * can be useful for diagnosing problems by tracing back the chain of
     * events leading up to this event.
     *
     * @param scheduledEventId The ID of the <code>LambdaFunctionScheduled</code> event that was
     *         recorded when this AWS Lambda function was scheduled. This information
     *         can be useful for diagnosing problems by tracing back the chain of
     *         events leading up to this event.
     */
    public void setScheduledEventId(Long scheduledEventId) {
        this.scheduledEventId = scheduledEventId;
    }
    
    /**
     * The ID of the <code>LambdaFunctionScheduled</code> event that was
     * recorded when this AWS Lambda function was scheduled. This information
     * can be useful for diagnosing problems by tracing back the chain of
     * events leading up to this event.
     * <p>
     * Returns a reference to this object so that method calls can be chained together.
     *
     * @param scheduledEventId The ID of the <code>LambdaFunctionScheduled</code> event that was
     *         recorded when this AWS Lambda function was scheduled. This information
     *         can be useful for diagnosing problems by tracing back the chain of
     *         events leading up to this event.
     *
     * @return A reference to this updated object so that method calls can be chained
     *         together.
     */
    public StartLambdaFunctionFailedEventAttributes withScheduledEventId(Long scheduledEventId) {
        this.scheduledEventId = scheduledEventId;
        return this;
    }

    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     *
     * @return The cause of the failure. This information is generated by the system
     *         and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     *         set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     *         sufficient permissions. For details and example IAM policies, see <a
     *         href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     *         IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     *
     * @see StartLambdaFunctionFailedCause
     */
    public String getCause() {
        return cause;
    }
    
    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     *
     * @param cause The cause of the failure. This information is generated by the system
     *         and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     *         set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     *         sufficient permissions. For details and example IAM policies, see <a
     *         href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     *         IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     *
     * @see StartLambdaFunctionFailedCause
     */
    public void setCause(String cause) {
        this.cause = cause;
    }
    
    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * Returns a reference to this object so that method calls can be chained together.
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     *
     * @param cause The cause of the failure. This information is generated by the system
     *         and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     *         set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     *         sufficient permissions. For details and example IAM policies, see <a
     *         href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     *         IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     *
     * @return A reference to this updated object so that method calls can be chained
     *         together.
     *
     * @see StartLambdaFunctionFailedCause
     */
    public StartLambdaFunctionFailedEventAttributes withCause(String cause) {
        this.cause = cause;
        return this;
    }

    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     *
     * @param cause The cause of the failure. This information is generated by the system
     *         and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     *         set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     *         sufficient permissions. For details and example IAM policies, see <a
     *         href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     *         IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     *
     * @see StartLambdaFunctionFailedCause
     */
    public void setCause(StartLambdaFunctionFailedCause cause) {
        this.cause = cause.toString();
    }
    
    /**
     * The cause of the failure. This information is generated by the system
     * and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     * set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     * sufficient permissions. For details and example IAM policies, see <a
     * href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     * IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     * <p>
     * Returns a reference to this object so that method calls can be chained together.
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Allowed Values: </b>ASSUME_ROLE_FAILED
     *
     * @param cause The cause of the failure. This information is generated by the system
     *         and can be useful for diagnostic purposes. <note>If <b>cause</b> is
     *         set to OPERATION_NOT_PERMITTED, the decision failed because it lacked
     *         sufficient permissions. For details and example IAM policies, see <a
     *         href="http://docs.aws.amazon.com/amazonswf/latest/developerguide/swf-dev-iam.html">Using
     *         IAM to Manage Access to Amazon SWF Workflows</a>.</note>
     *
     * @return A reference to this updated object so that method calls can be chained
     *         together.
     *
     * @see StartLambdaFunctionFailedCause
     */
    public StartLambdaFunctionFailedEventAttributes withCause(StartLambdaFunctionFailedCause cause) {
        this.cause = cause.toString();
        return this;
    }

    /**
     * The error message (if any).
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Length: </b>0 - 1728<br/>
     *
     * @return The error message (if any).
     */
    public String getMessage() {
        return message;
    }
    
    /**
     * The error message (if any).
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Length: </b>0 - 1728<br/>
     *
     * @param message The error message (if any).
     */
    public void setMessage(String message) {
        this.message = message;
    }
    
    /**
     * The error message (if any).
     * <p>
     * Returns a reference to this object so that method calls can be chained together.
     * <p>
     * <b>Constraints:</b><br/>
     * <b>Length: </b>0 - 1728<br/>
     *
     * @param message The error message (if any).
     *
     * @return A reference to this updated object so that method calls can be chained
     *         together.
     */
    public StartLambdaFunctionFailedEventAttributes withMessage(String message) {
        this.message = message;
        return this;
    }

    /**
     * Returns a string representation of this object; useful for testing and
     * debugging.
     *
     * @return A string representation of this object.
     *
     * @see java.lang.Object#toString()
     */
    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        if (getScheduledEventId() != null) sb.append("ScheduledEventId: " + getScheduledEventId() + ",");
        if (getCause() != null) sb.append("Cause: " + getCause() + ",");
        if (getMessage() != null) sb.append("Message: " + getMessage() );
        sb.append("}");
        return sb.toString();
    }
    
    @Override
    public int hashCode() {
        final int prime = 31;
        int hashCode = 1;
        
        hashCode = prime * hashCode + ((getScheduledEventId() == null) ? 0 : getScheduledEventId().hashCode()); 
        hashCode = prime * hashCode + ((getCause() == null) ? 0 : getCause().hashCode()); 
        hashCode = prime * hashCode + ((getMessage() == null) ? 0 : getMessage().hashCode()); 
        return hashCode;
    }
    
    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj == null) return false;

        if (obj instanceof StartLambdaFunctionFailedEventAttributes == false) return false;
        StartLambdaFunctionFailedEventAttributes other = (StartLambdaFunctionFailedEventAttributes)obj;
        
        if (other.getScheduledEventId() == null ^ this.getScheduledEventId() == null) return false;
        if (other.getScheduledEventId() != null && other.getScheduledEventId().equals(this.getScheduledEventId()) == false) return false; 
        if (other.getCause() == null ^ this.getCause() == null) return false;
        if (other.getCause() != null && other.getCause().equals(this.getCause()) == false) return false; 
        if (other.getMessage() == null ^ this.getMessage() == null) return false;
        if (other.getMessage() != null && other.getMessage().equals(this.getMessage()) == false) return false; 
        return true;
    }
}
    